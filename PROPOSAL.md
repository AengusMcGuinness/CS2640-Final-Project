# Progress Update

## Project Status

Since writing this proposal, I have built the first working baseline of the system. The repository now includes a thread-safe in-memory key-value store, a TCP client/server pair, and a small smoke test. The server has also been refactored from a one-thread-per-connection model to a single-threaded, coroutine-driven event loop using nonblocking sockets. This is an important step because it makes the TCP baseline closer to the structure of a high-performance server and gives me a cleaner foundation for comparing against RDMA later.

## What Has Been Implemented

The main pieces completed so far are:

- a `KeyValueStore` backed by `std::unordered_map`
- request parsing for `SET`, `GET`, `DEL`, and `QUIT`
- a line-oriented TCP protocol with simple text responses
- a client executable for interactive testing
- a server executable that accepts connections and processes requests
- a coroutine/event-loop layer that uses `poll` to wait for socket readiness
- a unit test that validates basic key-value store behavior

The code is organized so that storage, protocol, and transport logic are separated. That separation matters for the rest of the project because it lets me swap in two-sided RDMA and one-sided RDMA without redesigning the application semantics.

## Preliminary Results

I have already verified the baseline in three ways:

- the project builds successfully with CMake
- the key-value store unit test passes
- an end-to-end client/server smoke test succeeds over localhost

In the smoke test, the server correctly handled `SET foo bar`, `GET foo`, `DEL foo`, and `QUIT`, returning `OK`, `VALUE bar`, `OK`, `NOT_FOUND`, and `BYE` in order. This confirms that the request parser, server dispatch path, store, and response formatting are all wired together correctly.

## Challenges So Far

The biggest technical challenge has been designing the server in a way that is simple enough to implement now but still realistic for later performance work. A thread-per-connection server is easy to write, but it would distort performance results and make the TCP baseline less comparable to RDMA. To address that, I switched to a nonblocking event-loop design using C++20 coroutines.

That refactor introduced a few practical issues:

- the project had to be moved from C++17 to C++20 to enable coroutines
- socket descriptors had to be made nonblocking
- the event loop had to safely resume suspended coroutines when sockets became readable or writable
- the server needed careful handling of partial reads and partial writes

Another challenge is keeping the implementation measurable. Since the project is about comparing communication mechanisms, I need to avoid mixing transport overhead with unrelated bottlenecks. For that reason, I am keeping the protocol simple and the key-value operations minimal at this stage.

## Next Steps

The next major step is to add a benchmark harness that can generate controlled workloads and measure throughput and latency under different access patterns. After that, I plan to define a stable memory layout for remote objects and begin the RDMA implementation. I also want to simulate metadata updates, such as recency tracking, so I can measure how cache-eviction-style bookkeeping affects the performance advantage of one-sided reads.

---

# Distributed Caching with RDMA

## Problem

Distributed caching systems such as Memcached, Redis, and Valkey are widely used to reduce latency and improve throughput in modern cloud infrastructure. However, their scalability is often limited by the cost of maintaining cache eviction metadata. Traditional eviction policies such as Least Recently Used (LRU) maintain ordering structures that must be updated whenever a cached object is accessed. In distributed deployments, these updates require remote communication and server CPU involvement, increasing latency and limiting throughput.

Modern data center networks increasingly support Remote Direct Memory Access (RDMA), which allows machines to read or write remote memory without involving the remote CPU. In principle, RDMA could allow cache reads to be served using one-sided memory operations, enabling clients to directly access cached data stored on remote machines. However, traditional caching architectures prevent this optimization because reads must also update metadata structures.

This project investigates how metadata updates interact with RDMA communication primitives and evaluates whether one-sided RDMA reads can significantly reduce the overhead of distributed cache operations.

---

## Proposed Approach

This project will implement a minimal distributed key–value store prototype designed to evaluate the performance impact of different communication mechanisms and metadata update strategies.

The prototype will consist of a server that exposes a memory region containing a hash table of key–value objects. Clients will perform lookup requests using different communication mechanisms depending on the experiment configuration.

### System Configurations

1. **RPC-based key–value store**  
   A baseline implementation in which clients send requests to the server using RPC-style communication. The server processes requests and returns values.

2. **Two-sided RDMA key–value store**  
   A version in which requests are sent using RDMA send/receive operations. This allows measurement of performance improvements from RDMA messaging compared with RPC.

3. **One-sided RDMA reads**  
   A version in which clients directly read key–value objects from remote memory using RDMA read operations without involving the server CPU.

To simulate the effect of cache eviction policies, the system will optionally perform lightweight metadata updates such as atomic counters or linked-list operations that approximate the overhead of maintaining recency information. This allows the project to measure how metadata updates interact with RDMA communication mechanisms.

---

## Evaluation Plan

The system will be evaluated using synthetic workloads designed to mimic common caching access patterns.

Experiments will vary:
- Request rates
- Key popularity distributions (using Zipfian workloads)

### Metrics

The evaluation will measure:
- Throughput (requests per second)
- Median request latency
- Tail latency (95th or 99th percentile)
- CPU utilization
- Network overhead

These experiments will isolate the performance impact of communication mechanisms and metadata updates.

The experiments will be conducted on **CloudLab**, which provides access to RDMA-capable machines suitable for distributed systems experiments.

---

## Project Goals

- **75% Goal**  
  Implement a basic distributed key–value store and measure the performance difference between RPC-based communication and two-sided RDMA messaging.

- **100% Goal**  
  Extend the system to support one-sided RDMA reads and evaluate the performance improvements compared to RPC and two-sided RDMA communication.

- **125% Goal**  
  Simulate metadata updates associated with cache eviction policies and measure how these updates affect the performance benefits of one-sided RDMA reads.
