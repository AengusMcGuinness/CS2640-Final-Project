#include "kvstore/kv_store.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "test failed: " << message << '\n';
        return 1;
    }
    return 0;
}

struct StartGate {
    std::mutex mutex;
    std::condition_variable cv;
    bool go = false;
};

}  // namespace

int main() {
    kvstore::KeyValueStore store;
    constexpr std::size_t thread_count = 8;
    constexpr std::size_t iterations = 500;

    StartGate gate;
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t t = 0; t < thread_count; ++t) {
        threads.emplace_back([&, t]() {
            {
                std::unique_lock<std::mutex> lock(gate.mutex);
                gate.cv.wait(lock, [&] { return gate.go; });
            }

            for (std::size_t i = 0; i < iterations; ++i) {
                const std::string key = "thread_" + std::to_string(t) + "_key_" + std::to_string(i);
                const std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);

                store.set(key, value);
                auto loaded = store.get(key);
                if (!loaded.has_value() || *loaded != value) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                if (!store.erase(key)) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.go = true;
    }
    gate.cv.notify_all();

    for (auto& thread : threads) {
        thread.join();
    }

    if (failed.load(std::memory_order_relaxed)) {
        std::cerr << "test failed: concurrent read/write mismatch\n";
        return EXIT_FAILURE;
    }

    if (int rc = expect(store.size() == 0, "store should end empty after concurrent operations"); rc != 0) {
        return rc;
    }

    std::cout << "kv_store_concurrency_test passed\n";
    return EXIT_SUCCESS;
}
