# Implementation Walkthrough

This document explains how the project was implemented, file by file, and why each piece exists. The overall goal has stayed the same from the proposal: build a TCP baseline for a distributed key-value store, measure it carefully, and then use that baseline as the comparison point for two-sided RDMA and one-sided RDMA later.

The main design principle is separation of concerns. Storage, protocol, transport, benchmarking, testing, and documentation are kept separate so the TCP baseline stays understandable and the later RDMA work can swap the transport without rewriting the store or benchmark harness.

## Build And Documentation Files

### `CMakeLists.txt`

This file defines the build. It sets the project to C++20 because coroutines are used in the event loop, creates a reusable `project_core` library, and builds the server, client, tests, and benchmark executables. The point is to keep the project modular instead of turning it into one monolithic binary, which also makes it easier to add RDMA targets later.

### `README.md`

This is the quick-start guide. It explains how to build the project, run the server and client, execute tests, run the benchmark, and generate plots. It is intentionally practical because later experiments depend on being able to rebuild and rerun the system reliably.

### `PROPOSAL.md`

	This is the original project proposal with a progress update at the top. It remains important because the checkpoint and final report build on the same research goal, and the progress section shows how the actual implementation evolved.

### `CHECKPOINT.md`

This is the midterm checkpoint document. It summarizes the current code, the baseline measurements, the main implementation challenges, and the proposed RDMA strategy. It is more technical than the proposal because it includes actual data.

### `docs/IMPLEMENTATION_OVERVIEW.md`

This is a higher-level technical overview of the architecture. It explains the storage layer, protocol layer, transport layer, event loop, and how the baseline connects to later RDMA phases.

## Storage Layer

### `include/kvstore/kv_store.hpp`

This header declares the key-value store interface: `set`, `get`, `erase`, and `size`. It is separate from the implementation so the rest of the program only needs the interface, not the storage internals.

### `src/kvstore/kv_store.cpp`

This file implements the storage logic with `std::unordered_map<std::string, std::string>` and a `std::mutex`. It stays intentionally simple so the project has a correct, thread-safe baseline before any RDMA-specific optimization work.

## Protocol Layer

### `include/protocol/text_protocol.hpp`

This header defines the request and response types plus the parser/serializer declarations. It gives the network layer a stable contract so transport code does not need to understand command syntax.

### `src/protocol/text_protocol.cpp`

This file implements the text protocol. It parses commands like `GET alpha`, `SET beta value`, `DEL gamma`, and `QUIT`, and it serializes responses like `OK`, `VALUE ...`, `NOT_FOUND`, `ERROR ...`, and `BYE`. A plain-text protocol is a good fit because it is easy to debug and benchmark.

## Socket Utilities

### `include/net/socket_utils.hpp`

This header declares the socket helper functions used by the server, client, event loop, and benchmark harness. Keeping the helpers here lets the project share the low-level socket logic consistently.

### `src/net/socket_utils.cpp`

This file implements the POSIX socket helpers. It creates TCP sockets, resolves and connects clients, and provides `read_line`, `write_all`, and nonblocking setup. The nonblocking server socket is important because it lets the event loop sleep in `poll` instead of blocking a thread per client.

## Event Loop

### `include/net/event_loop.hpp`

This header declares the coroutine event loop and the awaiter types used by server coroutines. It is a small runtime that lets the server wait for socket readiness cleanly.

### `src/net/event_loop.cpp`

This file implements the coroutine scheduler. It uses `poll` to wait for readiness, stores suspended coroutines by file descriptor, and resumes them when the kernel reports activity. This replaces one-thread-per-connection with an event-driven model that is closer to scalable servers and to RDMA completion handling.

## Server And Client

### `src/server/main.cpp`

This is the main TCP server. It creates the listening socket, starts the event loop, accepts clients, and launches one coroutine per connection. The request logic is kept separate from transport through `handle_request`, which is what makes it reusable for later RDMA work. The server also handles partial reads and writes, which is necessary for nonblocking sockets.

### `src/client/main.cpp`

This is the interactive client. It connects to the server, sends typed commands, and prints the responses. It is mainly for debugging and manual verification rather than benchmarking.

## Benchmarking

### `benchmarks/kv_benchmark.cpp`

This file is the benchmark driver. It can prefill the keyspace, spawn worker threads, generate GET and SET requests with configurable access patterns, measure latency, and write results to CSV. It is important because the same harness can later be reused for RDMA comparisons if the request semantics stay stable.

### `scripts/plot_benchmark.py`

This script reads the benchmark CSV and generates graphs for throughput, latency percentiles, and error counts. It exists so the benchmark data is easy to visualize instead of staying buried in raw numbers.

## Tests

### `tests/kv_store_test.cpp`

This test covers basic store behavior: empty startup, insert, overwrite, and erase. It verifies the core data structure before any transport logic is involved.

### `tests/protocol_test.cpp`

This test exercises the protocol parser and serializer. It checks valid commands, invalid commands, and exact response strings. This keeps the request/response contract stable.

### `tests/kv_store_concurrency_test.cpp`

This test stresses the store from multiple threads. It verifies the mutex protection around the unordered map under concurrent access.

## Generated Data And Outputs

### `experiments/baseline_clients.csv`

This file is the first collected benchmark dataset and contains the raw measurements behind the plots and checkpoint results.

### `plots/baseline_clients/throughput_vs_clients.png`

This plot shows how throughput changes as the client count increases.

### `plots/baseline_clients/latency_vs_clients.png`

This plot shows mean, median, and tail latency across client counts.

### `plots/baseline_clients/errors_vs_clients.png`

This plot shows that the measured and warmup error counts remain at zero.

## Repository Hygiene

### `.gitignore`

This file keeps generated files out of version control, such as build artifacts, virtual environments, and local cache directories.

## Why The Implementation Was Structured This Way

The implementation is organized around the research goal: keep the baseline clean and repeatable so transport comparisons are meaningful. The store, protocol, event loop, benchmark harness, and tests each do one job, which also sets up the later RDMA work because the transport can change without rewriting the application semantics.

## Current State And Next Steps

At this point, the project has a working TCP baseline, a correct storage layer, a tested protocol, a benchmark harness, and CSV output with plots. The next step is to extend this baseline into the RDMA phases: prepare registered memory and completion-queue logic for two-sided RDMA, define a stable object layout for one-sided reads, add optional metadata-update overhead, and run the final comparisons.

