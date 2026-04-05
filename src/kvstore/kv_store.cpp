#include "kvstore/kv_store.hpp"

namespace kvstore {

bool KeyValueStore::set(std::string key, std::string value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = entries_.insert_or_assign(std::move(key), std::move(value));
    (void)it;
    return inserted;
}

std::optional<std::string> KeyValueStore::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool KeyValueStore::erase(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.erase(key) > 0;
}

std::size_t KeyValueStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

}  // namespace kvstore
