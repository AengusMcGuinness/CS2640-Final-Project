#pragma once

// libibverbs is the user-space RDMA verbs API. It is only present on Linux
// systems with an RDMA-capable NIC and the rdma-core package installed.
#include <infiniband/verbs.h>

#include <cstdint>
#include <cstring>
#include <string_view>

namespace net {

// Wire-format structure exchanged over a TCP side-channel before RDMA begins.
// Both peers send and receive one of these to establish QP connectivity.
// The rkey and addr fields are only meaningful in the server's copy and are
// used by clients for one-sided RDMA READ/WRITE operations.
struct QpInfo {
    uint32_t qp_num;    // queue pair number on the sending side
    uint16_t lid;       // local identifier (0 for pure RoCE fabrics)
    uint8_t  gid[16];   // GID in IPv6 format; index 0 is used for RoCE v2
    uint32_t rkey;      // remote key for the exported memory region (one-sided)
    uint64_t addr;      // base virtual address of the exported region (one-sided)
    uint64_t num_slots; // number of RdmaSlots in the exported region (one-sided)
};

// Maximum number of bytes per RDMA message.
// Sized to comfortably hold any single protocol request or response.
static constexpr std::size_t RDMA_MSG_SIZE = 4096;

// RdmaContext owns one Reliable Connected (RC) queue pair and the pinned send
// and receive buffers that back it.  Each peer-to-peer connection gets its
// own RdmaContext.
//
// Typical two-sided usage (server and client mirror each other):
//   ctx.init();
//   local = ctx.local_info();          // send to peer over TCP
//   ctx.connect(remote_info);          // received from peer over TCP
//   ctx.post_recv();                   // arm the receive buffer
//   ctx.post_send("GET foo\n");        // send a request
//   ctx.poll_completion();             // wait for send to complete
//   ctx.poll_completion();             // wait for receive to complete
//   auto reply = ctx.recv_data();      // inspect the received bytes
//
// One-sided READ usage (client only, after connect()):
//   ctx.post_rdma_read(remote_addr + offset, remote_rkey, local_buf, len);
//   ctx.poll_completion();
//
// One-sided WRITE usage is symmetric: register a local source buffer, post the
// write to the remote slot address, then poll the signaled completion.
class RdmaContext {
public:
    RdmaContext()  = default;
    ~RdmaContext() { destroy(); }

    // Not copyable or movable: libibverbs resources are tied to the addresses
    // of the pinned send_buf_ and recv_buf_ members.
    RdmaContext(const RdmaContext&)            = delete;
    RdmaContext& operator=(const RdmaContext&) = delete;

    // Open the RDMA device and allocate a QP.
    // Pass nullptr to use the first device found (correct on single-NIC nodes).
    // Transitions the QP from RESET to INIT.
    bool init(const char* device_name = nullptr);

    // Return the local QP info to send to the remote peer.
    // Call after init() and, if exporting a memory region for one-sided reads,
    // after calling set_exported_region() so that rkey/addr/num_slots are filled.
    QpInfo local_info() const;

    // Record the memory region exported for one-sided client reads so that
    // local_info() can include rkey, addr, and num_slots.
    void set_exported_region(const ibv_mr* mr, uint64_t num_slots);

    // Transition the QP from INIT through RTR to RTS using the remote peer's info.
    // Must be called after init() and before posting any sends or receives.
    bool connect(const QpInfo& remote);

    // Pre-post a receive work request to catch the next inbound send.
    // Must be called before the peer posts its matching send.
    bool post_recv();

    // Copy data into the send buffer and issue an RDMA SEND to the peer.
    bool post_send(std::string_view data);

    // Issue an RDMA READ that copies len bytes from remote memory into local_dst.
    // local_lkey must be the lkey of a pre-registered MR that covers local_dst.
    // The MR must remain registered until after poll_completion() returns,
    // because the NIC DMAs into it asynchronously after this call returns.
    bool post_rdma_read(uint64_t remote_addr, uint32_t remote_rkey,
                        void* local_dst, uint32_t len, uint32_t local_lkey);

    // Issue an RDMA WRITE that copies len bytes from local_src into remote memory.
    // local_lkey must be the lkey of a pre-registered MR that covers local_src.
    // The MR must remain registered until after poll_completion() returns.
    bool post_rdma_write(uint64_t remote_addr, uint32_t remote_rkey,
                         const void* local_src, uint32_t len, uint32_t local_lkey);

    // Issue an RDMA FETCH_AND_ADD on an 8-byte counter at remote_addr.
    // local_lkey must be the lkey of a pre-registered MR that covers local_dst.
    // The MR must remain registered until after poll_completion() returns.
    bool post_fetch_and_add(uint64_t remote_addr, uint32_t remote_rkey,
                            void* local_dst, uint64_t add_val, uint32_t local_lkey);

    // Block-spin the completion queue until one completion event appears.
    // Returns false on error or non-success work completion status.
    bool poll_completion();

    // Register a buffer with this context's protection domain.
    // The returned ibv_mr must be kept alive until all in-flight operations
    // that use it have been polled to completion, then deregistered by the caller.
    ibv_mr* reg_mr(void* buf, std::size_t len, int access_flags);

    // View the receive buffer after a successful recv completion.
    std::string_view recv_data() const;

    // Release all libibverbs resources in reverse-allocation order.
    void destroy();

    // Expose the protection domain so callers can register additional memory.
    ibv_pd* pd() const { return pd_; }

private:
    ibv_context* ctx_     = nullptr;
    ibv_pd*      pd_      = nullptr;
    ibv_cq*      cq_      = nullptr;
    ibv_qp*      qp_      = nullptr;
    ibv_mr*      send_mr_ = nullptr;
    ibv_mr*      recv_mr_ = nullptr;

    // Pinned message buffers - must not be moved while the MRs are registered.
    char     send_buf_[RDMA_MSG_SIZE] = {};
    char     recv_buf_[RDMA_MSG_SIZE] = {};
    uint32_t recv_len_                = 0;

    // Metadata about the exported one-sided region (set by set_exported_region).
    uint32_t exported_rkey_      = 0;
    uint64_t exported_addr_      = 0;
    uint64_t exported_num_slots_ = 0;

    uint8_t port_num_ = 1;
};

} // namespace net
