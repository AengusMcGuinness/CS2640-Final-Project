#include "kvstore/kv_store.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "test failed: " << message << '\n';
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    kvstore::KeyValueStore store;

    if (int rc = expect(store.size() == 0, "new store should be empty"); rc != 0) {
        return rc;
    }

    store.set("alpha", "one");
    if (int rc = expect(store.size() == 1, "store size should update after set"); rc != 0) {
        return rc;
    }

    auto value = store.get("alpha");
    if (int rc = expect(value.has_value(), "value should exist after set"); rc != 0) {
        return rc;
    }
    if (int rc = expect(*value == "one", "retrieved value should match"); rc != 0) {
        return rc;
    }

    store.set("alpha", "two");
    value = store.get("alpha");
    if (int rc = expect(value.has_value() && *value == "two", "set should overwrite existing values"); rc != 0) {
        return rc;
    }

    if (int rc = expect(store.erase("alpha"), "erase should report success for existing keys"); rc != 0) {
        return rc;
    }
    if (int rc = expect(!store.get("alpha").has_value(), "erased key should be missing"); rc != 0) {
        return rc;
    }
    if (int rc = expect(!store.erase("alpha"), "erase should fail for missing keys"); rc != 0) {
        return rc;
    }

    std::cout << "kv_store_test passed\n";
    return EXIT_SUCCESS;
}
