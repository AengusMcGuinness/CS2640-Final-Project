#pragma once

// libibverbs is required for memory registration.
#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kvstore {

// Maximum key and value lengths stored in each slot.
// Fixed sizes allow remote clients to calculate exact byte offsets without
// involving the server CPU, which is the core requirement for one-sided reads.
//
// RDMA_VALUE_MAX = 896 supports value sizes up to 895 bytes, covering the
// benchmark sweep (32, 64, 128, 512 bytes).  Slots are 1024 bytes, a power
// of two, which keeps offset arithmetic simple and avoids false sharing.
static constexpr std::size_t RDMA_KEY_MAX   = 112;
static constexpr std::size_t RDMA_VALUE_MAX = 896;

// Number of slots in the hash table.  Must be a power of two so the hash
// index can be computed with a bitwise AND instead of a modulo.
// 4096 slots x 1024 bytes = 4 MB registered memory region.
static constexpr std::size_t RDMA_NUM_SLOTS = 4096;

// One entry in the flat, RDMA-readable open-addressing hash table.
//
// Memory layout (1024 bytes total, 1024-byte aligned):
//   offset   0 : occupied     (1 byte)   0=empty, 1=valid, 2=tombstone
//   offset   1 : _pad         (7 bytes)  alignment padding
//   offset   8 : access_count (8 bytes)  updated by FETCH_AND_ADD (LRU simulation)
//   offset  16 : key          (112 bytes) null-terminated
//   offset 128 : value        (896 bytes) null-terminated
//   total 1024 bytes
//
// access_count is at offset 8, satisfying the 8-byte alignment requirement
// for RDMA FETCH_AND_ADD atomics on Mellanox ConnectX hardware.
struct alignas(1024) RdmaSlot {
    uint8_t  occupied;              // 0=empty, 1=valid, 2=tombstone
    uint8_t  _pad[7];               // explicit padding for access_count alignment
    uint64_t access_count;          // incremented atomically to simulate LRU metadata
    char     key  [RDMA_KEY_MAX];   // null-terminated key   (max 111 chars)
    char     value[RDMA_VALUE_MAX]; // null-terminated value (max 895 chars)
};
static_assert(sizeof(RdmaSlot) == 1024, "RdmaSlot must be exactly 1024 bytes");
static_assert(offsetof(RdmaSlot, access_count) == 8,
              "access_count must be at offset 8 for atomic alignment");

// RdmaStore is a flat open-addressing hash table laid out in a single
// contiguous array of RdmaSlots.  The array is registered with libibverbs so
// that remote clients can issue one-sided RDMA READ operations directly into
// it without server CPU involvement.
//
// The server interacts with the store through set/get/erase.
// Clients compute the same FNV-1a hash and probe offset arithmetic locally,
// then issue RDMA READs to read slots directly.
//
// After registering memory, call register_mr() to obtain the rkey and base
// address needed by remote clients (passed to them over the TCP side-channel).
class RdmaStore {
public:
    RdmaStore();

    // Insert or overwrite a key-value pair.
    // Returns false if the key or value exceeds the fixed slot limits,
    // or if the table is full.
    bool set(std::string_view key, std::string_view value);

    // Look up a key.  Returns nullopt if the key is not present.
    std::optional<std::string> get(std::string_view key) const;

    // Remove a key by writing a tombstone entry so linear probing still works
    // correctly for other keys that hashed to the same chain.
    bool erase(std::string_view key);

    // Register the slot array with the given protection domain.
    // Must be called before handing the rkey/addr to remote clients.
    // The returned ibv_mr* is owned by the caller and must be deregistered
    // when the server shuts down.
    ibv_mr* register_mr(ibv_pd* pd);

    // Return a pointer to the start of the slot array (for address arithmetic).
    const RdmaSlot* slots() const { return slots_; }
    RdmaSlot*       slots()       { return slots_; }

    // Total size in bytes of the registered region (4096 * 1024 = 4 MB).
    static constexpr std::size_t region_size() {
        return RDMA_NUM_SLOTS * sizeof(RdmaSlot);
    }

private:
    // FNV-1a hash of a null-terminated string, modulo RDMA_NUM_SLOTS.
    static std::size_t slot_index(std::string_view key);

    // The contiguous slot array.  alignas on RdmaSlot is propagated here
    // because the array inherits the element's alignment.
    RdmaSlot slots_[RDMA_NUM_SLOTS] = {};
};

} // namespace kvstore
