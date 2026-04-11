#include "kvstore/kv_store.hpp"
#include "net/event_loop.hpp"
#include "net/socket_utils.hpp"
#include "protocol/text_protocol.hpp"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <exception>
#include <iostream>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using net::EventLoop;

protocol::Response handle_request(kvstore::KeyValueStore& store, const protocol::Request& request) {
    using protocol::Response;
    using protocol::ResponseType;
    using protocol::RequestType;

    switch (request.type) {
    case RequestType::Get: {
        auto value = store.get(request.key);
        if (!value) {
            return {ResponseType::NotFound, {}};
        }
        return {ResponseType::Value, *value};
    }
    case RequestType::Set:
        store.set(request.key, request.value);
        return {ResponseType::Ok, {}};
    case RequestType::Del:
        return store.erase(request.key) ? Response{ResponseType::Ok, {}} : Response{ResponseType::NotFound, {}};
    case RequestType::Quit:
        return {ResponseType::Bye, {}};
    case RequestType::Invalid:
        return {ResponseType::Error, request.error};
    }

    return {ResponseType::Error, "unhandled request"};
}

net::EventLoop::Task handle_client(EventLoop& loop, kvstore::KeyValueStore& store, int client_fd) {
    std::string inbound;
    inbound.reserve(4096);
    char buffer[4096];

    while (true) {
        while (true) {
            const ssize_t bytes_read = ::recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                inbound.append(buffer, static_cast<std::size_t>(bytes_read));
                continue;
            }

            if (bytes_read == 0) {
                net::close_socket(client_fd);
                co_return;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            net::close_socket(client_fd);
            co_return;
        }

        std::size_t newline = std::string::npos;
        while ((newline = inbound.find('\n')) != std::string::npos) {
            std::string line = inbound.substr(0, newline);
            inbound.erase(0, newline + 1);

            const protocol::Request request = protocol::parse_request(line);
            const protocol::Response response = handle_request(store, request);
            const std::string serialized = protocol::serialize_response(response);

            std::size_t offset = 0;
            while (offset < serialized.size()) {
                const ssize_t bytes_written = ::send(
                    client_fd,
                    serialized.data() + offset,
                    serialized.size() - offset,
                    0
                );

                if (bytes_written > 0) {
                    offset += static_cast<std::size_t>(bytes_written);
                    continue;
                }

                if (bytes_written < 0 && errno == EINTR) {
                    continue;
                }

                if (bytes_written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    co_await loop.writable(client_fd);
                    continue;
                }

                net::close_socket(client_fd);
                co_return;
            }

            if (request.type == protocol::RequestType::Quit) {
                net::close_socket(client_fd);
                co_return;
            }
        }

        co_await loop.readable(client_fd);
    }
}

net::EventLoop::Task accept_loop(EventLoop& loop, kvstore::KeyValueStore& store, int server_fd) {
    while (true) {
        co_await loop.readable(server_fd);

        while (true) {
            sockaddr_storage client_addr {};
            socklen_t client_len = sizeof(client_addr);
            const int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd >= 0) {
                if (!net::set_non_blocking(client_fd)) {
                    net::close_socket(client_fd);
                    continue;
                }

                loop.spawn(handle_client(loop, store, client_fd));
                continue;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            break;
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::uint16_t port = static_cast<std::uint16_t>(argc > 1 ? std::stoi(argv[1]) : 9090);

    int server_fd = net::create_server_socket(port);
    if (server_fd < 0) {
        std::cerr << "failed to start server on port " << port << ": " << std::strerror(errno) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "kv_server listening on port " << port << '\n';

    kvstore::KeyValueStore store;
    EventLoop loop;
    loop.spawn(accept_loop(loop, store, server_fd));
    loop.run();
    net::close_socket(server_fd);
    return EXIT_SUCCESS;
}
