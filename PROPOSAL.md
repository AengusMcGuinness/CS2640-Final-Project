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
