# Midterm Checkpoint Report

This checkpoint updates the original proposal with the current state of the project, the first performance results, and a concrete plan for the RDMA phases. The project is still focused on the same research question from the proposal: how does distributed cache performance change when communication moves from ordinary TCP/RPC to two-sided RDMA and then to one-sided RDMA reads?

## Project Status

The repository has moved from a proposal-only stage to a working experimental baseline. The current code now includes:

- a thread-safe in-memory key-value store
- a line-oriented text protocol for `SET`, `GET`, `DEL`, and `QUIT`
- a TCP server and interactive client
- a single-threaded, coroutine-driven event loop for the server
- a benchmark harness with CSV export
- correctness tests for the store, protocol, and concurrent access
- baseline plots generated from benchmark data

The current implementation is still the TCP/RPC baseline, not RDMA. That is intentional: the point at this stage is to establish a clean correctness and performance reference before changing transports.

## Current Code

The most important files are:

- [src/server/main.cpp](src/server/main.cpp): server entry point, accept loop, and per-connection coroutine
- [src/net/event_loop.cpp](src/net/event_loop.cpp): coroutine scheduler built on `poll`
- [src/net/socket_utils.cpp](src/net/socket_utils.cpp): TCP socket helpers and nonblocking setup
- [src/protocol/text_protocol.cpp](src/protocol/text_protocol.cpp): text parsing and response serialization
- [src/kvstore/kv_store.cpp](src/kvstore/kv_store.cpp): mutex-protected in-memory key-value store
- [benchmarks/kv_benchmark.cpp](benchmarks/kv_benchmark.cpp): workload generator and CSV writer

The server is still TCP-based. It uses `AF_INET` / `SOCK_STREAM` sockets and the usual `accept`, `recv`, and `send` calls, but it no longer uses one thread per client. Instead, a single event loop waits for socket readiness and resumes coroutines when sockets become readable or writable. That design is much closer to a realistic high-performance networking baseline and avoids mixing transport overhead with thread-creation overhead.

## What Has Been Completed

### Storage and Protocol

The storage layer is a simple `std::unordered_map<std::string, std::string>` protected by a mutex. The API stays small on purpose:

- `set(key, value)`
- `get(key)`
- `erase(key)`
- `size()`

The protocol layer converts plain-text commands into structured requests and serializes responses back into text. This makes the system easy to debug and gives us a stable semantics layer that can later be reused by RDMA transports.

### Server Architecture

The server flow is:

1. Wait for the listening socket to become readable.
2. Accept as many pending client connections as possible.
3. Put accepted sockets into nonblocking mode.
4. Spawn a coroutine per client.
5. Read bytes until a full newline-delimited request is available.
6. Parse the request, update the store, and send the response.
7. Suspend if the socket would block.

This design is much better for performance experiments than a thread-per-connection server because it reduces scheduler overhead and keeps the concurrency model explicit.

### Benchmarking and Correctness

The benchmark harness now supports:

- configurable client concurrency
- configurable request counts
- configurable GET/SET mix
- Zipfian or uniform key access
- warmup operations
- CSV export for plotting

The test suite currently covers:

- basic store behavior
- protocol parsing and serialization
- concurrent access to the store

## Preliminary Results

The first baseline dataset was collected locally over TCP using:

- `clients`: 1, 2, 4, 8
- `operations`: 2000 per run
- `keys`: 256
- `value_size`: 32 bytes
- `get_ratio`: 0.95
- `zipf_s`: 0.8
- `warmup`: 200
- `prefill`: enabled

The raw results are in [experiments/baseline_clients.csv](experiments/baseline_clients.csv), and the graphs are in [plots/baseline_clients/](plots/baseline_clients).

### Throughput

Plot: [throughput_vs_clients.png](plots/baseline_clients/throughput_vs_clients.png)

Throughput rises from 1 client to 4 clients:

- 1 client: about 16.9k requests/sec
- 2 clients: about 27.8k requests/sec
- 4 clients: about 45.3k requests/sec

At 8 clients, throughput drops slightly to about 42.5k requests/sec. That suggests the server is reaching a contention point, likely from a combination of event-loop scheduling overhead and the mutex-protected shared store. The important point is that the system is scaling for a while, but not indefinitely, which gives us a meaningful baseline shape to compare against future RDMA versions.

### Latency

Plot: [latency_vs_clients.png](plots/baseline_clients/latency_vs_clients.png)

Latency grows with concurrency, and the tail gets much worse at 8 clients:

- mean latency: 52.3 us, 63.7 us, 76.3 us, 166.7 us
- p50 latency: 52 us, 63 us, 70 us, 143 us
- p95 latency: 62 us, 76 us, 123 us, 322 us
- p99 latency: 68 us, 128 us, 163 us, 408 us

This is the most important baseline signal so far. Median latency stays relatively low at moderate load, but tail latency grows quickly as concurrency increases. For a project about distributed caching, tail behavior matters because cache services are often judged by consistency, not only average speed.

### Errors

Plot: [errors_vs_clients.png](plots/baseline_clients/errors_vs_clients.png)

The error plot stays flat at zero for both warmup and measured phases. That means:

- request parsing is working correctly
- the TCP request/response path is stable
- the benchmark harness is issuing valid commands
- the store is not losing data under the tested concurrent workload

That zero-error result is important because it means the throughput and latency numbers are valid, not artifacts of failed requests.

## Preliminary RDMA Strategy

Based on the current code structure, the best RDMA plan is to add transports in stages without changing the storage semantics.

### Two-Sided RDMA Strategy

The two-sided RDMA phase should mirror the current RPC semantics as closely as possible. The goal is not to redesign the application, but to replace TCP message passing with RDMA send/receive while keeping the request format and store behavior stable.

The proposed approach is:

1. **Keep the request semantics the same.**
   - A request should still represent `GET`, `SET`, `DEL`, or `QUIT`.
   - That keeps benchmark comparisons fair across TCP and RDMA.

2. **Use registered memory for request and response buffers.**
   - RDMA works best when the server and client exchange messages through pinned, registered buffers.
   - The next code changes should introduce a memory-registration layer separate from the store itself.

3. **Create an RDMA connection setup path.**
   - The project will need queue pairs, completion queues, and a way to exchange connection metadata.
   - This likely means using `libibverbs` and probably `librdmacm` for connection management.

4. **Implement a dedicated server loop for completions.**
   - Instead of `poll` on sockets, the server will poll RDMA completion queues.
   - Incoming send/receive completions should dispatch to the same request handler that the TCP baseline uses today.

5. **Keep the response path minimal.**
   - A small request and response format will make it easier to compare TCP and RDMA fairly.
   - The benchmark harness can reuse the same client-side workload generation code.

This is the safest path because it isolates transport complexity while preserving the current application behavior.

### One-Sided RDMA Strategy

The one-sided phase should come after the data layout is stable. For one-sided reads, the client should be able to read a remote value directly from a registered memory region without involving the server CPU on each read.

To make that possible, the project needs:

- a fixed memory layout for keys and values
- stable object offsets or an index table
- a clear rule for how metadata is updated, if at all
- a way to decide when an entry is safe to read without server intervention

This is also where the eviction/metadata question becomes important. If every read still has to update some recency structure, the one-sided read advantage may shrink. That is exactly why the proposal includes a metadata-update experiment.

## Challenges So Far

The main challenge has been balancing realism and scope.

The original thread-per-connection prototype was easy to write, but it would have made the TCP baseline much less meaningful for performance evaluation. Refactoring to a coroutine/event-loop design fixed that, but it required:

- moving the project to C++20
- handling nonblocking sockets carefully
- dealing with partial reads and partial writes
- preserving correctness under concurrent access

Benchmarking also needed some care. To make the results useful, the harness had to support warmup, concurrency sweeps, CSV export, and a stable workload shape. Without those pieces, the plots would not be good enough for the later TCP-vs-RDMA comparison.

## Next Steps

The next work items are:

1. **Expand the benchmark sweep**
   - try more client counts
   - vary `get_ratio`
   - compare uniform and Zipfian workloads
   - collect more CSV rows for stronger graphs

2. **Prepare the RDMA transport layer**
   - introduce memory registration and connection metadata exchange
   - build two-sided RDMA send/receive first
   - keep the current request handler unchanged

3. **Design the one-sided memory layout**
   - define fixed-size records or an index-based layout
   - decide what metadata is required for correctness
   - measure how metadata updates affect performance

4. **Run the final comparison**
   - TCP baseline
   - two-sided RDMA
   - one-sided RDMA
   - with and without metadata updates

## Conclusion

The project is in a solid midterm state. The TCP baseline is working, the core data path is correct, the benchmark harness is collecting useful numbers, and the current plots already show a clear scaling story. The next step is to extend this stable baseline into two-sided RDMA and then one-sided RDMA so the final comparison is meaningful and technically defensible.

