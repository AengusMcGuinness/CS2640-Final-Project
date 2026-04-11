#pragma once

// `cstddef` gives us `std::size_t` for the `size()` method.
#include <cstddef>
// `mutex` provides the lock type used to make the store thread-safe.
#include <mutex>
// `optional` lets `get()` return "missing" without using a sentinel string.
#include <optional>
// `string` is the basic storage type for keys and values.
#include <string>
// `unordered_map` provides the hash table backing store.
#include <unordered_map>

// Keep the key-value store inside its own namespace so its API stays explicit.
namespace kvstore {

// A small thread-safe in-memory key-value store used by the server and tests.
class KeyValueStore {
public:
    // Insert or overwrite a key/value pair.
    bool set(std::string key, std::string value);
    // Look up a key and return nothing if it does not exist.
    std::optional<std::string> get(const std::string& key) const;
    // Remove a key if it exists.
    bool erase(const std::string& key);
    // Report how many entries are currently stored.
    std::size_t size() const;

private:
    // `mutable` allows const member functions to still take the lock.
    mutable std::mutex mutex_;
    // The actual key/value data structure.
    std::unordered_map<std::string, std::string> entries_;
};

// Close the namespace to avoid leaking implementation names globally.
}  // namespace kvstore
