#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace kvstore {

class KeyValueStore {
public:
    bool set(std::string key, std::string value);
    std::optional<std::string> get(const std::string& key) const;
    bool erase(const std::string& key);
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> entries_;
};

}  // namespace kvstore
