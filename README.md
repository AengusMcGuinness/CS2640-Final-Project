# CS2640 Final Project

This repository now contains the first working baseline for the project:

- a thread-safe in-memory key-value store
- a simple line-based TCP server
- an interactive client
- a smoke test for the store
- a technical implementation overview in [docs/IMPLEMENTATION_OVERVIEW.md](./docs/IMPLEMENTATION_OVERVIEW.md)

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run the server

```bash
./build/kv_server 9090
```

## Run the client

In another terminal:

```bash
./build/kv_client 127.0.0.1 9090
```

Example commands:

```text
SET foo bar
GET foo
DEL foo
QUIT
```

## Run the tests

```bash
ctest --test-dir build
```

## Current scope

This is the RPC baseline for the project. The RDMA implementations and benchmarking harness can be layered on top of this core without changing the key-value store interface.
