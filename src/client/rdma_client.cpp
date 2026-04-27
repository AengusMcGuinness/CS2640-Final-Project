// Interactive and benchmark RDMA key-value client.
//
// Supports two operational modes (--mode) and two execution styles
// (interactive vs. --benchmark):
//
//   --mode two-sided  (default)
//     Uses RDMA send/recv.  Interactive only; use kv_benchmark for
//     two-sided throughput and latency sweeps.
//
//   --mode one-sided
//     Interactive: GET / GET_META / QUIT at a prompt.
//     Benchmark:   add --benchmark to run a timed loop and emit a CSV row
//                  compatible with plot_benchmark.py.
//
// One-sided benchmark flags:
//   --benchmark          enable benchmark mode
//   --ops N              measured operations (default 10000)
//   --warmup N           warmup operations discarded before timing (default 1000)
//   --keys N             key-space size; must match --preload on the server (default 1024)
//   --metadata           also issue FETCH_AND_ADD after each read (LRU simulation)
//   --csv PATH           append one result row to PATH (creates header if needed)
//
// Usage examples:
//   # Interactive two-sided
//   kv_client_rdma --host <ip> --mode two-sided --device mlx5_0
//
//   # Interactive one-sided smoke test
//   kv_client_rdma --host <ip> --mode one-sided --device mlx5_0
//
//   # One-sided benchmark, no metadata, 4 client sweeps
//   kv_client_rdma --host <ip> --mode one-sided --device mlx5_0 \
//                  --benchmark --ops 10000 --warmup 1000 --keys 1024 \
//                  --csv experiments/rdma_one_sided_clients.csv
//
//   # One-sided benchmark with LRU metadata overhead
//   kv_client_rdma --host <ip> --mode one-sided --device mlx5_0 \
//                  --benchmark --ops 10000 --warmup 1000 --keys 1024 \
//                  --metadata \
//                  --csv experiments/rdma_one_sided_metadata.csv

#include "kvstore/rdma_store.hpp"
#include "net/rdma_context.hpp"
#include "protocol/text_protocol.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <numeric>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using Clock        = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;

namespace {

// ---------------------------------------------------------------------------
// TCP helpers for the QP-info handshake
// ---------------------------------------------------------------------------

int tcp_connect(const char* host, uint16_t port) {
    addrinfo hints    = {};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo*   res      = nullptr;
    std::string port_str = std::to_string(port);
    if (::getaddrinfo(host, port_str.c_str(), &hints, &res) != 0) return -1;

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

bool write_all(int fd, const void* buf, std::size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; return false; }
        p += n; len -= static_cast<std::size_t>(n);
    }
    return true;
}

bool read_all(int fd, void* buf, std::size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = ::read(fd, p, len);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; return false; }
        p += n; len -= static_cast<std::size_t>(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// FNV-1a hash -- must match RdmaStore::slot_index exactly so the client
// computes the same slot index as the server without any server involvement.
std::size_t fnv1a(std::string_view key) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : key) {
        hash ^= c;
        hash *= 16777619u;
    }
    return static_cast<std::size_t>(hash) & (kvstore::RDMA_NUM_SLOTS - 1);
}

// Produce the same key strings the server uses when preloading the store.
// rdma_main.cpp preloads with "key" + std::to_string(i).
std::string make_key(std::size_t i) {
    return "key" + std::to_string(i);
}

// Perform one complete one-sided GET: probe the server's slot array via RDMA
// READs until the key is found or an empty slot is reached.
// Returns true if the key was found, false otherwise.
// If metadata_addr is non-zero, also issues a FETCH_AND_ADD on access_count
// to simulate LRU update overhead.
// Perform one complete one-sided GET via RDMA READs.
// slot_lkey and atomic_lkey must be the lkeys of MRs registered by the caller
// that cover local_slot and atomic_result respectively.  Both MRs must remain
// registered for the duration of this call (and until poll_completion returns).
bool one_sided_get(net::RdmaContext&      ctx,
                   const net::QpInfo&     server_info,
                   kvstore::RdmaSlot&     local_slot,
                   uint32_t               slot_lkey,
                   std::string_view       key,
                   bool                   simulate_metadata,
                   uint64_t&              atomic_result,
                   uint32_t               atomic_lkey)
{
    for (std::size_t i = 0; i < kvstore::RDMA_NUM_SLOTS; ++i) {
        std::size_t idx    = (fnv1a(key) + i) & (kvstore::RDMA_NUM_SLOTS - 1);
        uint64_t    offset = static_cast<uint64_t>(idx) * sizeof(kvstore::RdmaSlot);
        uint64_t    addr   = server_info.addr + offset;

        // Post the RDMA READ and poll its completion before inspecting the slot.
        // The MR (slot_lkey) must remain registered across both calls.
        if (!ctx.post_rdma_read(addr, server_info.rkey,
                                &local_slot, sizeof(local_slot), slot_lkey))
            return false;
        if (!ctx.poll_completion()) return false;

        if (local_slot.occupied == 0) return false; // empty slot: key absent
        if (local_slot.occupied == 2) continue;     // tombstone: keep probing

        if (std::strncmp(local_slot.key, key.data(), kvstore::RDMA_KEY_MAX) == 0) {
            if (simulate_metadata) {
                // RDMA FETCH_AND_ADD on access_count simulates LRU bookkeeping.
                // The server NIC performs the atomic; the server CPU stays idle.
                uint64_t count_addr = addr + offsetof(kvstore::RdmaSlot, access_count);
                ctx.post_fetch_and_add(count_addr, server_info.rkey,
                                       &atomic_result, 1, atomic_lkey);
                ctx.poll_completion();
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// CSV output (same column layout as kv_benchmark for plot_benchmark.py)
// ---------------------------------------------------------------------------

void write_csv_row(const std::string& path,
                   const char*        host,
                   uint16_t           port,
                   std::size_t        ops,
                   std::size_t        key_count,
                   std::size_t        warmup,
                   bool               metadata,
                   double             elapsed_s,
                   double             throughput,
                   double             mean_us,
                   double             p50_us,
                   double             p95_us,
                   double             p99_us,
                   std::size_t        ok_ops,
                   std::size_t        errors)
{
    std::ifstream probe(path);
    bool write_header = !probe.good() ||
                        probe.peek() == std::ifstream::traits_type::eof();

    std::ofstream out(path, std::ios::app);
    if (!out) {
        std::cerr << "kv_client_rdma: cannot open csv: " << path << '\n';
        return;
    }

    if (write_header) {
        out << "host,port,clients,operations,keys,value_size,get_ratio,zipf_s,"
               "warmup,prefill,elapsed_seconds,throughput_rps,mean_latency_us,"
               "p50_latency_us,p95_latency_us,p99_latency_us,"
               "measured_ok_responses,measured_errors,"
               "warmup_ok_responses,warmup_errors\n";
    }

    // Fixed fields for one-sided RDMA: single client, pure GET, no zipf skew.
    out << host           << ','   // host
        << port           << ','   // port
        << 1              << ','   // clients (always 1 per process for one-sided)
        << ops            << ','   // operations
        << key_count      << ','   // keys
        << 0              << ','   // value_size (not tracked in one-sided path)
        << 1.0            << ','   // get_ratio (always 1.0 -- pure GET)
        << 0.0            << ','   // zipf_s (uniform random key selection)
        << warmup         << ','   // warmup
        << 1              << ','   // prefill (server always preloads)
        << elapsed_s      << ','
        << throughput     << ','
        << mean_us        << ','
        << p50_us         << ','
        << p95_us         << ','
        << p99_us         << ','
        << ok_ops         << ','   // measured_ok_responses
        << errors         << ','   // measured_errors
        << warmup         << ','   // warmup_ok_responses (assume all ok)
        << 0              << '\n'; // warmup_errors
}

// ---------------------------------------------------------------------------
// Two-sided interactive loop
// ---------------------------------------------------------------------------

void run_two_sided(net::RdmaContext& ctx) {
    std::cout << "Connected (two-sided RDMA).\n"
              << "Commands: GET <k>  SET <k> <v>  DEL <k>  QUIT\n";

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        if (!ctx.post_recv()) { std::cerr << "post_recv failed\n"; break; }
        if (!ctx.post_send(line + "\n")) { std::cerr << "post_send failed\n"; break; }
        if (!ctx.poll_completion()) { std::cerr << "send completion error\n"; break; }
        if (!ctx.poll_completion()) { std::cerr << "recv completion error\n"; break; }

        std::cout << ctx.recv_data() << '\n';
        if (ctx.recv_data().find("BYE") != std::string_view::npos) break;
    }
}

// ---------------------------------------------------------------------------
// One-sided interactive loop
// ---------------------------------------------------------------------------

void run_one_sided_interactive(net::RdmaContext&   ctx,
                               const net::QpInfo&  server_info)
{
    std::cout << "Connected (one-sided RDMA).\n"
              << "  rkey=0x"  << std::hex << server_info.rkey
              << "  base=0x"  << server_info.addr
              << std::dec     << "  slots=" << server_info.num_slots << '\n'
              << "Commands: GET <key>   GET_META <key>   QUIT\n";

    kvstore::RdmaSlot local_slot = {};
    ibv_mr* slot_mr = ctx.reg_mr(&local_slot, sizeof(local_slot),
                                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!slot_mr) { std::cerr << "reg_mr failed\n"; return; }

    uint64_t atomic_result = 0;
    ibv_mr*  atomic_mr = ctx.reg_mr(&atomic_result, sizeof(atomic_result),
                                    IBV_ACCESS_LOCAL_WRITE);
    if (!atomic_mr) { ibv_dereg_mr(slot_mr); std::cerr << "reg_mr failed\n"; return; }

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "QUIT") break;

        bool simulate_metadata = false;
        std::string key;

        if (line.rfind("GET_META ", 0) == 0) {
            key               = line.substr(9);
            simulate_metadata = true;
        } else if (line.rfind("GET ", 0) == 0) {
            key = line.substr(4);
        } else {
            std::cout << "one-sided mode supports: GET, GET_META, QUIT\n";
            continue;
        }

        bool found = one_sided_get(ctx, server_info, local_slot, slot_mr->lkey,
                                   key, simulate_metadata,
                                   atomic_result, atomic_mr->lkey);
        if (found) {
            std::cout << "VALUE " << local_slot.value << '\n';
            if (simulate_metadata)
                std::cout << "  [access_count was " << atomic_result << "]\n";
        } else {
            std::cout << "NOT_FOUND\n";
        }
    }

    ibv_dereg_mr(slot_mr);
    ibv_dereg_mr(atomic_mr);
}

// ---------------------------------------------------------------------------
// One-sided benchmark loop
// ---------------------------------------------------------------------------

void run_one_sided_benchmark(net::RdmaContext&   ctx,
                             const net::QpInfo&  server_info,
                             const char*         host,
                             uint16_t            port,
                             std::size_t         ops,
                             std::size_t         warmup_count,
                             std::size_t         key_count,
                             bool                simulate_metadata,
                             const std::string&  csv_path)
{
    std::cout << "One-sided RDMA benchmark\n"
              << "  ops="     << ops
              << "  warmup="  << warmup_count
              << "  keys="    << key_count
              << "  metadata=" << (simulate_metadata ? "yes" : "no") << '\n';

    // Register a local slot buffer for RDMA READ destinations.
    kvstore::RdmaSlot local_slot = {};
    ibv_mr* slot_mr = ctx.reg_mr(&local_slot, sizeof(local_slot),
                                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!slot_mr) { std::cerr << "reg_mr(slot) failed\n"; return; }

    // Register an atomic result buffer for optional FETCH_AND_ADD.
    uint64_t atomic_result = 0;
    ibv_mr*  atomic_mr     = nullptr;
    if (simulate_metadata) {
        atomic_mr = ctx.reg_mr(&atomic_result, sizeof(atomic_result),
                               IBV_ACCESS_LOCAL_WRITE);
        if (!atomic_mr) {
            ibv_dereg_mr(slot_mr);
            std::cerr << "reg_mr(atomic) failed\n";
            return;
        }
    }

    // Use a seeded RNG for reproducible key selection.
    std::mt19937_64 rng(0xC0FFEEULL);
    std::uniform_int_distribution<std::size_t> key_dist(0, key_count - 1);

    // Warmup phase: exercise the RDMA path without recording latency.
    std::cout << "  warming up...\n";
    std::size_t warmup_errors = 0;
    for (std::size_t i = 0; i < warmup_count; ++i) {
        std::string key = make_key(key_dist(rng));
        bool ok = one_sided_get(ctx, server_info, local_slot, slot_mr->lkey,
                                key, simulate_metadata,
                                atomic_result,
                                atomic_mr ? atomic_mr->lkey : 0u);
        if (!ok) ++warmup_errors;
    }
    if (warmup_errors > 0)
        std::cerr << "  warning: " << warmup_errors << " warmup misses\n";

    // Measured phase: record per-operation latency in microseconds.
    std::vector<int64_t> latencies;
    latencies.reserve(ops);
    std::size_t ok_count    = 0;
    std::size_t error_count = 0;

    std::cout << "  measuring...\n";
    const auto wall_start = Clock::now();

    for (std::size_t i = 0; i < ops; ++i) {
        std::string key = make_key(key_dist(rng));

        const auto t0 = Clock::now();
        bool ok = one_sided_get(ctx, server_info, local_slot, slot_mr->lkey,
                                key, simulate_metadata,
                                atomic_result,
                                atomic_mr ? atomic_mr->lkey : 0u);
        const auto t1 = Clock::now();

        latencies.push_back(
            std::chrono::duration_cast<Microseconds>(t1 - t0).count());

        if (ok) ++ok_count;
        else    ++error_count;
    }

    const auto wall_end = Clock::now();
    double elapsed_s = std::chrono::duration_cast<
        std::chrono::duration<double>>(wall_end - wall_start).count();
    double throughput = static_cast<double>(ops) / elapsed_s;

    // Compute statistics.
    std::sort(latencies.begin(), latencies.end());
    double mean_us = latencies.empty() ? 0.0
        : static_cast<double>(
              std::accumulate(latencies.begin(), latencies.end(), int64_t{0}))
          / static_cast<double>(latencies.size());

    auto percentile = [&](double frac) -> double {
        if (latencies.empty()) return 0.0;
        std::size_t idx = std::min<std::size_t>(
            latencies.size() - 1,
            static_cast<std::size_t>(frac * (latencies.size() - 1)));
        return static_cast<double>(latencies[idx]);
    };

    double p50 = percentile(0.50);
    double p95 = percentile(0.95);
    double p99 = percentile(0.99);

    // Print human-readable summary.
    std::cout << "\nResults\n"
              << "  elapsed_s:        " << elapsed_s  << '\n'
              << "  throughput_rps:   " << throughput  << '\n'
              << "  mean_latency_us:  " << mean_us     << '\n'
              << "  p50_latency_us:   " << p50         << '\n'
              << "  p95_latency_us:   " << p95         << '\n'
              << "  p99_latency_us:   " << p99         << '\n'
              << "  ok_ops:           " << ok_count    << '\n'
              << "  errors:           " << error_count << '\n';

    // Write CSV row if a path was given.
    if (!csv_path.empty()) {
        write_csv_row(csv_path, host, port, ops, key_count, warmup_count,
                      simulate_metadata, elapsed_s, throughput,
                      mean_us, p50, p95, p99, ok_count, error_count);
        std::cout << "  csv: " << csv_path << '\n';
    }

    if (atomic_mr) ibv_dereg_mr(atomic_mr);
    ibv_dereg_mr(slot_mr);
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const char* host              = "localhost";
    uint16_t    port              = 9091;
    bool        one_sided         = false;
    const char* device_name       = nullptr;
    bool        benchmark_mode    = false;
    std::size_t ops               = 10000;
    std::size_t warmup_count      = 1000;
    std::size_t key_count         = 1024;
    bool        simulate_metadata = false;
    std::string csv_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--host"     && i + 1 < argc) host              = argv[++i];
        if (arg == "--port"     && i + 1 < argc) port              = static_cast<uint16_t>(std::stoi(argv[++i]));
        if (arg == "--mode"     && i + 1 < argc) one_sided         = (std::string(argv[++i]) == "one-sided");
        if (arg == "--device"   && i + 1 < argc) device_name       = argv[++i];
        if (arg == "--benchmark")                benchmark_mode    = true;
        if (arg == "--ops"      && i + 1 < argc) ops               = static_cast<std::size_t>(std::stoul(argv[++i]));
        if (arg == "--warmup"   && i + 1 < argc) warmup_count      = static_cast<std::size_t>(std::stoul(argv[++i]));
        if (arg == "--keys"     && i + 1 < argc) key_count         = static_cast<std::size_t>(std::stoul(argv[++i]));
        if (arg == "--metadata")                 simulate_metadata = true;
        if (arg == "--csv"      && i + 1 < argc) csv_path          = argv[++i];
    }

    // Connect to server over TCP for the QP info handshake.
    int tcp_fd = tcp_connect(host, port);
    if (tcp_fd < 0) {
        std::cerr << "kv_client_rdma: cannot connect to " << host << ':' << port << '\n';
        return EXIT_FAILURE;
    }

    net::RdmaContext ctx;
    if (!ctx.init(device_name)) {
        ::close(tcp_fd);
        return EXIT_FAILURE;
    }

    // Server sends its QpInfo first; client replies with its own.
    net::QpInfo server_info = {};
    net::QpInfo local       = ctx.local_info();
    if (!read_all(tcp_fd, &server_info, sizeof(server_info)) ||
        !write_all(tcp_fd, &local,      sizeof(local))) {
        std::cerr << "kv_client_rdma: QP info exchange failed\n";
        ::close(tcp_fd);
        return EXIT_FAILURE;
    }
    ::close(tcp_fd);

    if (!ctx.connect(server_info)) {
        std::cerr << "kv_client_rdma: QP connect failed\n";
        return EXIT_FAILURE;
    }

    if (one_sided && benchmark_mode) {
        run_one_sided_benchmark(ctx, server_info, host, port,
                                ops, warmup_count, key_count,
                                simulate_metadata, csv_path);
    } else if (one_sided) {
        run_one_sided_interactive(ctx, server_info);
    } else {
        run_two_sided(ctx);
    }

    return EXIT_SUCCESS;
}
