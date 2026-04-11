#include "net/socket_utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9090;
    std::size_t clients = 4;
    std::size_t operations = 10000;
    std::size_t key_count = 1024;
    std::size_t value_size = 64;
    double get_ratio = 0.95;
    double zipf_s = 1.0;
    std::size_t warmup = 1000;
    bool prefill = true;
    std::string csv_path;
};

struct Result {
    std::vector<std::int64_t> latencies_us;
    std::size_t measured_ok_responses = 0;
    std::size_t measured_errors = 0;
    std::size_t warmup_ok_responses = 0;
    std::size_t warmup_errors = 0;
};

[[noreturn]] void usage_and_exit(const char* program) {
    std::cerr << "usage: " << program
              << " [--host HOST] [--port PORT] [--clients N] [--ops N] [--keys N]"
              << " [--value-size N] [--get-ratio R] [--zipf-s S] [--warmup N] [--no-prefill]"
              << " [--csv PATH]\n";
    std::exit(EXIT_FAILURE);
}

bool parse_uint(const std::string& text, std::size_t& out) {
    try {
        std::size_t index = 0;
        const unsigned long long value = std::stoull(text, &index);
        if (index != text.size()) {
            return false;
        }
        out = static_cast<std::size_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_port(const std::string& text, std::uint16_t& out) {
    std::size_t value = 0;
    if (!parse_uint(text, value) || value > 65535) {
        return false;
    }
    out = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_double(const std::string& text, double& out) {
    try {
        std::size_t index = 0;
        const double value = std::stod(text, &index);
        if (index != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

Options parse_args(int argc, char* argv[]) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << flag << '\n';
                usage_and_exit(argv[0]);
            }
            return argv[++i];
        };

        if (arg == "--host") {
            options.host = next_value("--host");
            continue;
        }
        if (arg == "--port") {
            if (!parse_port(next_value("--port"), options.port)) {
                std::cerr << "invalid port\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--clients") {
            if (!parse_uint(next_value("--clients"), options.clients) || options.clients == 0) {
                std::cerr << "invalid clients value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--ops") {
            if (!parse_uint(next_value("--ops"), options.operations) || options.operations == 0) {
                std::cerr << "invalid ops value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--keys") {
            if (!parse_uint(next_value("--keys"), options.key_count) || options.key_count == 0) {
                std::cerr << "invalid keys value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--value-size") {
            if (!parse_uint(next_value("--value-size"), options.value_size) || options.value_size == 0) {
                std::cerr << "invalid value-size value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--get-ratio") {
            if (!parse_double(next_value("--get-ratio"), options.get_ratio) || options.get_ratio < 0.0 || options.get_ratio > 1.0) {
                std::cerr << "invalid get-ratio value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--zipf-s") {
            if (!parse_double(next_value("--zipf-s"), options.zipf_s) || options.zipf_s < 0.0) {
                std::cerr << "invalid zipf-s value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--warmup") {
            if (!parse_uint(next_value("--warmup"), options.warmup)) {
                std::cerr << "invalid warmup value\n";
                usage_and_exit(argv[0]);
            }
            continue;
        }
        if (arg == "--no-prefill") {
            options.prefill = false;
            continue;
        }
        if (arg == "--csv") {
            options.csv_path = next_value("--csv");
            continue;
        }

        std::cerr << "unknown argument: " << arg << '\n';
        usage_and_exit(argv[0]);
    }

    return options;
}

std::string make_key(std::size_t index) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "key_%08zu", index);
    return std::string(buffer);
}

std::string make_value(std::size_t size, std::size_t seed) {
    std::string value(size, 'x');
    for (std::size_t i = 0; i < size; ++i) {
        value[i] = static_cast<char>('a' + ((seed + i) % 26));
    }
    return value;
}

std::vector<double> build_zipf_cdf(std::size_t key_count, double s) {
    std::vector<double> cdf(key_count, 0.0);
    if (key_count == 0) {
        return cdf;
    }

    double total = 0.0;
    for (std::size_t i = 0; i < key_count; ++i) {
        total += 1.0 / std::pow(static_cast<double>(i + 1), s);
    }

    double cumulative = 0.0;
    for (std::size_t i = 0; i < key_count; ++i) {
        cumulative += (1.0 / std::pow(static_cast<double>(i + 1), s)) / total;
        cdf[i] = cumulative;
    }

    cdf.back() = 1.0;
    return cdf;
}

std::size_t sample_key(std::mt19937_64& rng, const std::vector<double>& zipf_cdf) {
    if (zipf_cdf.empty()) {
        return 0;
    }

    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double sample = dist(rng);
    auto it = std::lower_bound(zipf_cdf.begin(), zipf_cdf.end(), sample);
    if (it == zipf_cdf.end()) {
        return zipf_cdf.size() - 1;
    }
    return static_cast<std::size_t>(std::distance(zipf_cdf.begin(), it));
}

bool request_response(int fd, std::string_view command, std::string& response) {
    if (!net::write_all(fd, std::string(command) + "\n")) {
        return false;
    }
    return net::read_line(fd, response);
}

bool response_starts_with(const std::string& response, std::string_view prefix) {
    return response.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), response.begin());
}

void write_csv_row(
    const std::string& path,
    const Options& options,
    double elapsed_seconds,
    double throughput,
    double mean_latency,
    double p50_latency,
    double p95_latency,
    double p99_latency,
    std::size_t measured_ok_responses,
    std::size_t measured_errors,
    std::size_t warmup_ok_responses,
    std::size_t warmup_errors
) {
    std::ifstream probe(path);
    const bool write_header = !probe.good() || probe.peek() == std::ifstream::traits_type::eof();
    std::ofstream out(path, std::ios::app);
    if (!out) {
        std::cerr << "failed to open csv output: " << path << '\n';
        std::exit(EXIT_FAILURE);
    }

    if (write_header) {
        out << "host,port,clients,operations,keys,value_size,get_ratio,zipf_s,warmup,prefill,elapsed_seconds,throughput_rps,mean_latency_us,p50_latency_us,p95_latency_us,p99_latency_us,measured_ok_responses,measured_errors,warmup_ok_responses,warmup_errors\n";
    }

    out << options.host << ','
        << options.port << ','
        << options.clients << ','
        << options.operations << ','
        << options.key_count << ','
        << options.value_size << ','
        << options.get_ratio << ','
        << options.zipf_s << ','
        << options.warmup << ','
        << (options.prefill ? 1 : 0) << ','
        << elapsed_seconds << ','
        << throughput << ','
        << mean_latency << ','
        << p50_latency << ','
        << p95_latency << ','
        << p99_latency << ','
        << measured_ok_responses << ','
        << measured_errors << ','
        << warmup_ok_responses << ','
        << warmup_errors << '\n';
}

Result run_worker(
    const Options& options,
    const std::vector<std::string>& keys,
    const std::vector<double>& zipf_cdf,
    std::size_t worker_index,
    std::size_t worker_ops,
    std::size_t warmup_ops
) {
    Result result;
    result.latencies_us.reserve(worker_ops);

    int fd = net::create_client_socket(options.host, options.port);
    if (fd < 0) {
        std::cerr << "failed to connect worker " << worker_index << '\n';
        result.measured_errors += worker_ops;
        return result;
    }

    std::mt19937_64 rng(0xC0FFEEULL + worker_index * 9973ULL);
    std::uniform_real_distribution<double> ratio_dist(0.0, 1.0);
    std::uniform_int_distribution<std::size_t> uniform_key_dist(0, keys.size() - 1);

    auto run_phase = [&](std::size_t count, bool measure) {
        for (std::size_t i = 0; i < count; ++i) {
            const bool do_get = ratio_dist(rng) < options.get_ratio;
            const std::size_t key_index = options.zipf_s > 0.0 ? sample_key(rng, zipf_cdf) : uniform_key_dist(rng);
            const std::string& key = keys[key_index];
            std::string command;
            if (do_get) {
                command = "GET " + key;
            } else {
                command = "SET " + key + " " + make_value(options.value_size, worker_index * 1315423911ULL + i);
            }

            const auto start = Clock::now();
            std::string response;
            const bool ok = request_response(fd, command, response);
            const auto end = Clock::now();

            if (!ok) {
                if (measure) {
                    ++result.measured_errors;
                } else {
                    ++result.warmup_errors;
                }
                continue;
            }

            if (do_get) {
                if (!response_starts_with(response, "VALUE ") && response != "NOT_FOUND") {
                    if (measure) {
                        ++result.measured_errors;
                    } else {
                        ++result.warmup_errors;
                    }
                } else {
                    if (measure) {
                        ++result.measured_ok_responses;
                    } else {
                        ++result.warmup_ok_responses;
                    }
                }
            } else {
                if (response != "OK") {
                    if (measure) {
                        ++result.measured_errors;
                    } else {
                        ++result.warmup_errors;
                    }
                } else {
                    if (measure) {
                        ++result.measured_ok_responses;
                    } else {
                        ++result.warmup_ok_responses;
                    }
                }
            }

            if (measure) {
                result.latencies_us.push_back(
                    std::chrono::duration_cast<Microseconds>(end - start).count()
                );
            }
        }
    };

    run_phase(warmup_ops, false);
    run_phase(worker_ops, true);

    net::close_socket(fd);
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    const Options options = parse_args(argc, argv);

    if (options.clients > options.operations) {
        std::cerr << "clients cannot exceed total operations\n";
        return EXIT_FAILURE;
    }

    std::vector<std::string> keys;
    keys.reserve(options.key_count);
    for (std::size_t i = 0; i < options.key_count; ++i) {
        keys.push_back(make_key(i));
    }

    const std::vector<double> zipf_cdf = build_zipf_cdf(options.key_count, options.zipf_s);

    if (options.prefill) {
        int fd = net::create_client_socket(options.host, options.port);
        if (fd < 0) {
            std::cerr << "failed to connect for prefill\n";
            return EXIT_FAILURE;
        }

        for (std::size_t i = 0; i < keys.size(); ++i) {
            std::string response;
            const std::string command = "SET " + keys[i] + " " + make_value(options.value_size, i);
            if (!request_response(fd, command, response) || response != "OK") {
                std::cerr << "prefill failed for key " << keys[i] << '\n';
                net::close_socket(fd);
                return EXIT_FAILURE;
            }
        }

        net::close_socket(fd);
    }

    const std::size_t base_ops = options.operations / options.clients;
    const std::size_t remainder = options.operations % options.clients;
    const std::size_t warmup_base = options.warmup / options.clients;
    const std::size_t warmup_remainder = options.warmup % options.clients;

    std::vector<std::thread> workers;
    std::vector<Result> results(options.clients);
    workers.reserve(options.clients);

    const auto start = Clock::now();
    for (std::size_t i = 0; i < options.clients; ++i) {
        const std::size_t worker_ops = base_ops + (i < remainder ? 1 : 0);
        const std::size_t worker_warmup = warmup_base + (i < warmup_remainder ? 1 : 0);

        workers.emplace_back([&, i, worker_ops, worker_warmup]() {
            results[i] = run_worker(options, keys, zipf_cdf, i, worker_ops, worker_warmup);
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }
    const auto end = Clock::now();

    std::vector<std::int64_t> latencies;
    std::size_t measured_ok_responses = 0;
    std::size_t measured_errors = 0;
    std::size_t warmup_ok_responses = 0;
    std::size_t warmup_errors = 0;

    for (const Result& result : results) {
        measured_ok_responses += result.measured_ok_responses;
        measured_errors += result.measured_errors;
        warmup_ok_responses += result.warmup_ok_responses;
        warmup_errors += result.warmup_errors;
        latencies.insert(latencies.end(), result.latencies_us.begin(), result.latencies_us.end());
    }

    std::sort(latencies.begin(), latencies.end());

    const double elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    const double throughput = static_cast<double>(options.operations) / elapsed_seconds;
    const auto mean_latency = latencies.empty()
        ? 0.0
        : static_cast<double>(std::accumulate(latencies.begin(), latencies.end(), std::int64_t{0})) / latencies.size();

    auto percentile = [&](double fraction) -> double {
        if (latencies.empty()) {
            return 0.0;
        }
        const std::size_t index = std::min<std::size_t>(
            latencies.size() - 1,
            static_cast<std::size_t>(std::floor(fraction * static_cast<double>(latencies.size() - 1)))
        );
        return static_cast<double>(latencies[index]);
    };

    std::cout << "Benchmark summary\n";
    std::cout << "  host: " << options.host << ':' << options.port << '\n';
    std::cout << "  clients: " << options.clients << '\n';
    std::cout << "  operations: " << options.operations << '\n';
    std::cout << "  keys: " << options.key_count << '\n';
    std::cout << "  get_ratio: " << options.get_ratio << '\n';
    std::cout << "  zipf_s: " << options.zipf_s << '\n';
    std::cout << "  throughput_rps: " << throughput << '\n';
    std::cout << "  mean_latency_us: " << mean_latency << '\n';
    std::cout << "  p50_latency_us: " << percentile(0.50) << '\n';
    std::cout << "  p95_latency_us: " << percentile(0.95) << '\n';
    std::cout << "  p99_latency_us: " << percentile(0.99) << '\n';
    std::cout << "  measured_ok_responses: " << measured_ok_responses << '\n';
    std::cout << "  measured_errors: " << measured_errors << '\n';
    std::cout << "  warmup_ok_responses: " << warmup_ok_responses << '\n';
    std::cout << "  warmup_errors: " << warmup_errors << '\n';

    if (!options.csv_path.empty()) {
        write_csv_row(
            options.csv_path,
            options,
            elapsed_seconds,
            throughput,
            mean_latency,
            percentile(0.50),
            percentile(0.95),
            percentile(0.99),
            measured_ok_responses,
            measured_errors,
            warmup_ok_responses,
            warmup_errors
        );
    }

    return (measured_errors + warmup_errors) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
