// RDMA key-value server.
//
// Supports two operational modes selected with --mode:
//
//   two-sided (default)
//     Uses RDMA send/recv for all client operations.  The protocol is
//     identical to the TCP baseline (same parse_request / serialize_response
//     paths), so throughput and latency differences isolate the transport.
//     One thread is spawned per client connection.
//
//   one-sided
//     Pre-populates the RdmaStore with a configurable number of key-value
//     pairs, registers the slot array as a remotely readable memory region,
//     and hands clients the rkey and base address over the TCP side-channel.
//     After the handshake, clients issue RDMA READs directly without any
//     server CPU involvement in the data path.
//
// Connection handshake (both modes):
//   1. Client opens a TCP connection to the server (default port 9091).
//   2. Server sends its QpInfo (including rkey/addr for one-sided mode).
//   3. Client sends its QpInfo.
//   4. Both sides transition their QPs to RTS.
//   5. TCP connection is closed; RDMA takes over.

#include "kvstore/rdma_store.hpp"
#include "net/rdma_context.hpp"
#include "protocol/text_protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Create a TCP server socket on the given port.
int make_tcp_listener(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(fd, 16) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// Write exactly len bytes to fd, retrying on EINTR.
bool write_all(int fd, const void* buf, std::size_t len) {
    const char* p   = static_cast<const char*>(buf);
    std::size_t rem = len;
    while (rem > 0) {
        ssize_t n = ::write(fd, p, rem);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        p   += n;
        rem -= static_cast<std::size_t>(n);
    }
    return true;
}

// Read exactly len bytes from fd, retrying on EINTR.
bool read_all(int fd, void* buf, std::size_t len) {
    char*       p   = static_cast<char*>(buf);
    std::size_t rem = len;
    while (rem > 0) {
        ssize_t n = ::read(fd, p, rem);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        p   += n;
        rem -= static_cast<std::size_t>(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Two-sided client handler (runs in its own thread)
// ---------------------------------------------------------------------------

// Perform the TCP handshake, then enter a request-response loop using
// RDMA send/recv.  Mirrors the TCP server's handle_client logic exactly
// so that benchmark comparisons reflect only transport differences.
void handle_two_sided_client(int tcp_fd, kvstore::RdmaStore& store,
                              const char* device_name) {
    auto ctx = std::make_unique<net::RdmaContext>();
    if (!ctx->init(device_name)) {
        ::close(tcp_fd);
        return;
    }

    // Exchange QP info over the TCP side-channel.
    net::QpInfo local = ctx->local_info();
    net::QpInfo remote = {};
    if (!write_all(tcp_fd, &local, sizeof(local)) ||
        !read_all(tcp_fd, &remote, sizeof(remote))) {
        std::cerr << "rdma server: QP info exchange failed\n";
        ::close(tcp_fd);
        return;
    }
    ::close(tcp_fd);

    if (!ctx->connect(remote)) return;

    std::cout << "rdma server: two-sided client connected (qpn="
              << remote.qp_num << ")\n";

    // Request-response loop.
    while (true) {
        // Arm a receive buffer before the client posts its send.
        if (!ctx->post_recv()) break;

        // Wait for the client's request to arrive.
        if (!ctx->poll_completion()) break;

        // Parse the request using the same protocol layer as the TCP server.
        std::string_view msg = ctx->recv_data();
        std::string      line(msg);
        // Strip trailing newline if present.
        if (!line.empty() && line.back() == '\n') line.pop_back();

        protocol::Request  req  = protocol::parse_request(line);
        protocol::Response resp;

        // Route request to the RdmaStore (set/get/erase operations).
        using protocol::RequestType;
        using protocol::ResponseType;
        switch (req.type) {
        case RequestType::Get: {
            auto v = store.get(req.key);
            resp = v ? protocol::Response{ResponseType::Value,   *v}
                     : protocol::Response{ResponseType::NotFound, {}};
            break;
        }
        case RequestType::Set:
            store.set(req.key, req.value);
            resp = {ResponseType::Ok, {}};
            break;
        case RequestType::Del:
            resp = store.erase(req.key)
                 ? protocol::Response{ResponseType::Ok, {}}
                 : protocol::Response{ResponseType::NotFound, {}};
            break;
        case RequestType::Quit:
            resp = {ResponseType::Bye, {}};
            break;
        case RequestType::Invalid:
        default:
            resp = {ResponseType::Error, req.error};
            break;
        }

        std::string serialized = protocol::serialize_response(resp);

        // Send the response and wait for the send completion.
        if (!ctx->post_send(serialized)) break;
        if (!ctx->poll_completion()) break;

        if (req.type == RequestType::Quit) break;
    }

    std::cout << "rdma server: two-sided client disconnected\n";
}

// ---------------------------------------------------------------------------
// One-sided setup handler (runs in its own thread)
// ---------------------------------------------------------------------------

// For one-sided RDMA, the server only needs to:
//   1. Create a QP (so the client can target this server's memory via RDMA READ).
//   2. Exchange QP info including the store's rkey and base address.
//   3. Transition to RTS and then become idle -- no CQ polling needed.
//
// After the handshake, the client issues RDMA READs independently.
void handle_one_sided_client(int tcp_fd, ibv_mr* store_mr,
                              uint64_t num_slots, const char* device_name) {
    auto ctx = std::make_unique<net::RdmaContext>();
    if (!ctx->init(device_name)) {
        ::close(tcp_fd);
        return;
    }

    // Annotate our QpInfo with the store's rkey and base address so the
    // client can issue RDMA READs against the slot array directly.
    ctx->set_exported_region(store_mr, num_slots);

    net::QpInfo local  = ctx->local_info();
    net::QpInfo remote = {};
    if (!write_all(tcp_fd, &local, sizeof(local)) ||
        !read_all(tcp_fd, &remote, sizeof(remote))) {
        std::cerr << "rdma server: one-sided QP info exchange failed\n";
        ::close(tcp_fd);
        return;
    }
    ::close(tcp_fd);

    if (!ctx->connect(remote)) return;

    std::cout << "rdma server: one-sided client connected (qpn=" << remote.qp_num
              << ")  rkey=0x" << std::hex << store_mr->rkey
              << " addr=0x"   << reinterpret_cast<uint64_t>(store_mr->addr)
              << std::dec << '\n';

    // The server is now idle on this path: the client drives all data movement.
    // Block here to keep the QP alive for the duration of the benchmark.
    // The client closes the control connection when it finishes.
    std::cout << "rdma server: one-sided data path active (server CPU idle)\n";

    // Keep the thread alive by waiting; the client will terminate on its own.
    // In a production system this would be replaced by a proper lifecycle signal.
    pause();
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    uint16_t    port        = 9091;
    bool        one_sided   = false;
    const char* device_name = nullptr;
    int         preload_n   = 10000; // keys to pre-load for one-sided benchmark

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--mode" && i + 1 < argc) {
            one_sided = (std::string(argv[++i]) == "one-sided");
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--device" && i + 1 < argc) {
            device_name = argv[++i];
        } else if (arg == "--preload" && i + 1 < argc) {
            preload_n = std::stoi(argv[++i]);
        }
    }

    // The store is shared across all client threads (set/get are mutex-free
    // here because the RDMA path is single-writer by convention; add locking
    // if concurrent server-side writes are needed).
    kvstore::RdmaStore store;

    // For one-sided mode, pre-populate the store so clients have data to read,
    // then register the slot array as a remotely accessible memory region.
    ibv_mr*   store_mr   = nullptr;
    ibv_pd*   shared_pd  = nullptr;
    ibv_context* shared_ctx = nullptr;

    if (one_sided) {
        std::cout << "rdma server: pre-loading " << preload_n << " key-value pairs\n";
        for (int i = 0; i < preload_n; ++i) {
            std::string k = "key"   + std::to_string(i);
            std::string v = "value" + std::to_string(i);
            store.set(k, v);
        }

        // Open a device context and PD just for registering the store region.
        // Client QPs use their own RdmaContext instances.
        int num_dev = 0;
        ibv_device** dev_list = ibv_get_device_list(&num_dev);
        if (!dev_list || num_dev == 0) {
            std::cerr << "rdma server: no RDMA devices found\n";
            return EXIT_FAILURE;
        }
        ibv_device* dev = dev_list[0];
        if (device_name) {
            for (int i = 0; i < num_dev; ++i) {
                if (std::strcmp(ibv_get_device_name(dev_list[i]), device_name) == 0) {
                    dev = dev_list[i];
                    break;
                }
            }
        }
        shared_ctx = ibv_open_device(dev);
        ibv_free_device_list(dev_list);
        if (!shared_ctx) {
            std::cerr << "rdma server: ibv_open_device failed\n";
            return EXIT_FAILURE;
        }
        shared_pd = ibv_alloc_pd(shared_ctx);
        if (!shared_pd) {
            std::cerr << "rdma server: ibv_alloc_pd failed\n";
            return EXIT_FAILURE;
        }
        store_mr = store.register_mr(shared_pd);
        if (!store_mr) return EXIT_FAILURE;

        std::cout << "rdma server: store registered  rkey=0x" << std::hex
                  << store_mr->rkey << "  addr=0x"
                  << reinterpret_cast<uint64_t>(store_mr->addr)
                  << std::dec
                  << "  size=" << kvstore::RdmaStore::region_size() << " bytes\n";
    }

    int listen_fd = make_tcp_listener(port);
    if (listen_fd < 0) {
        std::cerr << "rdma server: failed to bind port " << port
                  << ": " << std::strerror(errno) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "rdma server: listening on port " << port
              << "  mode=" << (one_sided ? "one-sided" : "two-sided") << '\n';

    while (true) {
        sockaddr_storage client_addr = {};
        socklen_t        client_len  = sizeof(client_addr);
        int client_fd = ::accept(listen_fd,
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            std::cerr << "rdma server: accept failed: " << std::strerror(errno) << '\n';
            break;
        }

        // Detach a thread for each client so connections are served concurrently.
        if (one_sided) {
            std::thread([client_fd, &store_mr, num_slots = (uint64_t)kvstore::RDMA_NUM_SLOTS, device_name]() {
                handle_one_sided_client(client_fd, store_mr, num_slots, device_name);
            }).detach();
        } else {
            std::thread([client_fd, &store, device_name]() {
                handle_two_sided_client(client_fd, store, device_name);
            }).detach();
        }
    }

    ::close(listen_fd);

    if (one_sided) {
        if (store_mr)   ibv_dereg_mr(store_mr);
        if (shared_pd)  ibv_dealloc_pd(shared_pd);
        if (shared_ctx) ibv_close_device(shared_ctx);
    }

    return EXIT_SUCCESS;
}
