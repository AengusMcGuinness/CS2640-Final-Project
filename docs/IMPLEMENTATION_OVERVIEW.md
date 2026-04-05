# Implementation Overview

This repository currently contains the first working baseline for the CS2640 final project: a thread-safe in-memory key-value store, a line-oriented TCP server/client pair, and a small smoke test. The design is intentionally simple so that later work on RDMA can reuse the same storage and protocol layers without rewriting the application logic.

## System Goal

The long-term research question is whether distributed cache operations can benefit from RDMA, especially one-sided reads that avoid server CPU involvement. To study that cleanly, the project needs a baseline system with a stable request model and measurable behavior. This baseline answers that requirement by separating the code into three layers:

1. **Storage layer**: the key-value store owns the data and is independent of the transport.
2. **Protocol layer**: text requests are parsed into structured commands and serialized back into responses.
3. **Transport layer**: the current implementation uses TCP, but the same interface can later be replaced by RDMA messaging or one-sided memory access.

That separation is the most important architectural choice in the repo because it keeps the comparison fair across RPC, two-sided RDMA, and one-sided RDMA.

## Project Layout

The code is organized to mirror those layers:

- `include/kvstore/` and `src/kvstore/`: storage interface and implementation
- `include/protocol/` and `src/protocol/`: request parsing and response formatting
- `include/net/` and `src/net/`: socket helper functions
- `src/server/`: server entry point and request handling loop
- `src/client/`: interactive client used for manual testing
- `tests/`: smoke test for the in-memory store

The build is handled by CMake in `CMakeLists.txt`, which defines:

- `project_core`: a reusable library containing the shared implementation
- `kv_server`: the TCP server executable
- `kv_client`: the interactive client executable
- `kvstore_tests`: a small executable used by `ctest`

## Storage Layer

The storage layer is implemented by `kvstore::KeyValueStore`. Internally it uses:

- `std::unordered_map<std::string, std::string>` for expected average O(1) key lookups
- `std::mutex` to serialize access from concurrent clients
- `std::optional<std::string>` as the return type for lookup operations that may miss

The public interface is intentionally small:

- `set(key, value)`: inserts or updates a value
- `get(key)`: returns the stored value if it exists
- `erase(key)`: deletes a key if it exists
- `size()`: returns the current number of entries

This is enough for the first baseline experiments and keeps the API easy to preserve when the transport changes later. For example, an RDMA implementation can still treat the key-value store as the semantic source of truth even if the read path bypasses the server CPU.

## Protocol Layer

The protocol is line-based and human-readable. Each request is one newline-terminated command:

```text
SET foo bar
GET foo
DEL foo
QUIT
```

`protocol::parse_request` converts a raw line into a structured `Request`:

- `RequestType::Get`
- `RequestType::Set`
- `RequestType::Del`
- `RequestType::Quit`
- `RequestType::Invalid`

The parser trims whitespace, normalizes the command name to lowercase, and validates the argument count. This prevents transport code from needing to understand command syntax. It only needs to read a line and pass it into the protocol layer.

`protocol::serialize_response` turns a structured `Response` back into a line of text:

- `OK`
- `VALUE <payload>`
- `NOT_FOUND`
- `ERROR <message>`
- `BYE`

This explicit response format is useful for debugging because it can be inspected with standard command-line tools. It also makes it easy to benchmark correctness before introducing binary RDMA formats.

## Transport Layer

The current transport is TCP, implemented using POSIX sockets in `src/net/socket_utils.cpp`.

Key helper functions:

- `create_server_socket(port, backlog)`: creates, binds, and listens on a socket
- `create_client_socket(host, port)`: resolves a host and connects
- `read_line(fd, line)`: reads one newline-delimited request from a stream socket
- `write_all(fd, data)`: ensures the full response is transmitted
- `close_socket(fd)`: closes the file descriptor safely

The server listens on a port, accepts clients, and processes each connection on its own detached `std::thread`. That concurrency model is simple and easy to reason about for a prototype. It is not the most scalable design, but it is enough to establish a working baseline and measure transport overhead.

## Request Flow

The end-to-end flow for a request is:

1. The client reads a command from standard input.
2. The client sends the line to the server over TCP.
3. The server reads one line from the socket.
4. The server parses the line into a typed request.
5. The server dispatches the request to the key-value store.
6. The server serializes the response and sends it back.
7. The client prints the response to the terminal.

For a `GET`, the hot path is small: parse, map lookup, response formatting. For a `SET` or `DEL`, the same path applies, with the additional cost of mutating the shared `unordered_map`.

## Concurrency Model

Concurrency is handled at two levels:

- **Per-store synchronization**: one mutex protects the map from concurrent modification and lookup races.
- **Per-connection threading**: each accepted TCP client runs in its own detached thread.

This design favors correctness and simplicity over maximum throughput. It is appropriate for a baseline because the project’s goal is to compare communication mechanisms, not to engineer the most optimized server possible. Later, the same storage interface could be paired with a thread pool, event loop, or asynchronous RDMA completion model if the experiments require it.

## Testing and Verification

The current test suite is a smoke test for the storage layer. It verifies:

- the store starts empty
- `set` inserts a key
- `get` returns the inserted value
- overwriting an existing key works
- `erase` removes a key and reports success or failure correctly

The repository was also verified with a real client/server interaction. That is important because unit tests alone do not prove the socket path is wired correctly. The smoke test confirms the RPC baseline is functional before RDMA work begins.

## Why This Structure Works for RDMA

The proposal’s later phases depend on keeping semantics stable while changing transport behavior:

- RPC baseline: request and response over TCP
- Two-sided RDMA: similar request/response semantics, but with RDMA send/receive
- One-sided RDMA: clients read remote memory directly

Because the protocol and store are already isolated, RDMA work can focus on the transport and memory layout instead of rethinking the whole application. In practice, the next major steps are to add a benchmark harness, define a fixed object layout for remote memory, and then introduce two-sided and one-sided RDMA paths that reuse the same workload generation code.

