# CS2640 Final Project

This repository now contains the first working baseline for the project:

- a thread-safe in-memory key-value store
- a coroutine-driven, event-loop-based line-oriented TCP server
- an interactive client
- a benchmark harness in `kv_benchmark`
- a smoke test for the store
- a detailed implementation walkthrough in [IMPLEMENTATION_DETAILS.md](./IMPLEMENTATION_DETAILS.md)
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

This currently runs:

- `kvstore_tests`
- `protocol_tests`
- `kv_store_concurrency_test`

## Run the benchmark

Start the server in one terminal, then run something like:

```bash
./build/kv_benchmark --host 127.0.0.1 --port 9090 --clients 4 --ops 10000 --keys 1024 --get-ratio 0.95 --zipf-s 1.0
```

Useful flags:

- `--clients N`: number of concurrent client connections
- `--ops N`: total timed operations
- `--keys N`: number of keys in the workload
- `--value-size N`: payload size for `SET`
- `--get-ratio R`: fraction of requests that are `GET`
- `--zipf-s S`: Zipf skew parameter, with `0` meaning uniform access
- `--warmup N`: warmup operations excluded from timing
- `--no-prefill`: skip preloading keys before the run
- `--csv PATH`: append a row of benchmark results to a CSV file

Example CSV run:

```bash
./build/kv_benchmark --host 127.0.0.1 --port 9090 --clients 8 --ops 20000 --csv results.csv
```

The CSV file contains one row per run with the workload settings and measured latency/throughput columns, which makes it easy to import into Excel, Google Sheets, Python, or R.

## Run CloudLab Experiment Suites

After building on both CloudLab nodes, start the matching server on the server node, then run one client-side script from the client node.

TCP:

```bash
# Server node:
./build/kv_server 9090

# Client node:
./scripts/run_tcp_experiments.sh --host "$SERVER_IP" --reset
```

Two-sided RDMA:

```bash
# Server node:
./build/kv_server_rdma --mode two-sided --device mlx5_0 --port 9091

# Client node:
./scripts/run_two_sided_rdma_experiments.sh --host "$SERVER_IP" --reset
```

One-sided RDMA:

```bash
# Server node:
./build/kv_server_rdma --mode one-sided --device mlx5_0 --port 9091 --preload 1024

# Client node:
./scripts/run_one_sided_rdma_experiments.sh --host "$SERVER_IP" --reset
```

Each script accepts `--help`, `--dry-run`, `--clients "1 2 4 8"`, `--outdir experiments`, and the usual benchmark knobs. The two-sided script runs the client-count, get-ratio, Zipf, and value-size sweeps; the one-sided script runs both no-metadata and metadata sweeps.

After downloading fresh CSVs, regenerate every available plot with:

```bash
./scripts/plot_all_results.sh
```

To download `experiments/` and any remote `plots/` directory from CloudLab:

```bash
./scripts/download_cluster_results.sh \
  --remote AMcG@hp080.utah.cloudlab.us \
  --local-dir /Users/aengusmcguinness/Desktop/CS2640-Final-Project
```

## Plot the CSV

If you have multiple benchmark runs in one CSV file, you can turn them into graphs with:

```bash
python3 -m pip install matplotlib
python3 scripts/plot_benchmark.py --csv results.csv --outdir plots --x clients
```

This writes:

- `throughput_vs_clients.png`
- `latency_vs_clients.png`
- `errors_vs_clients.png`

You can change `--x` to another numeric column such as `get_ratio` or `zipf_s` if you want to graph a different baseline dimension.

## Measure Server CPU

On the server node, run the server under the CPU sampler, then run one matching benchmark from the client node. Stop the sampler with `Ctrl-C` after the benchmark finishes:

```bash
python3 scripts/measure_cpu.py \
  --label "TCP" --transport tcp --clients 4 \
  --csv experiments/cpu_utilization.csv \
  -- ./build/kv_server 9090
```

For RDMA modes, replace the command after `--` with the matching `kv_server_rdma` command and label the row as `Two-Sided RDMA`, `One-Sided RDMA`, or `One-Sided RDMA + Metadata`. Then plot the CPU CSV:

```bash
python3 scripts/plot_cpu.py \
  --csv experiments/cpu_utilization.csv \
  --outdir plots/cpu \
  --x clients
```

## Convert Markdown To Word

If you want editable Word versions of the Markdown files, run:

```bash
./scripts/convert_md_to_docx.sh
```

This writes `.docx` files into `word-docx/` while preserving the folder structure. You can also choose a different output directory:

```bash
./scripts/convert_md_to_docx.sh --out-dir exports/docx
```

## Current scope

This is the RPC baseline for the project. The RDMA implementations and benchmarking harness can be layered on top of this core without changing the key-value store interface.

The server is implemented with C++20 coroutines and a single-threaded nonblocking event loop so that the TCP baseline is already closer to the structure of an eventual high-performance transport.
