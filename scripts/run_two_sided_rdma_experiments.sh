#!/usr/bin/env bash
# Run all two-sided RDMA client-side benchmark sweeps from the client node.
#
# Assumes the two-sided RDMA server is already running on the server node:
#   ./build/kv_server_rdma --mode two-sided --device mlx5_0 --port 9091

set -euo pipefail

HOST="${SERVER_IP:-}"
PORT="9091"
DEVICE="mlx5_0"
CLIENTS="1 2 4 8"
EXTRA_CLIENTS="4"
RATIOS="0.5 0.75 0.90 0.95 1.0"
ZIPFS="0.0 0.5 0.8 1.0 1.5"
VALUE_SIZES="32 64 128 512"
OPS="10000"
KEYS="1024"
VALUE_SIZE="64"
GET_RATIO="0.95"
ZIPF_S="0.8"
WARMUP="1000"
OUTDIR="experiments"
BUILD_DIR="build"
CLIENTS_CSV=""
RATIO_CSV=""
ZIPF_CSV=""
VALSIZE_CSV=""
RESET=0
DRY_RUN=0
ONLY_CLIENTS=0

usage() {
  cat <<'EOF'
Usage: scripts/run_two_sided_rdma_experiments.sh --host SERVER_IP [options]

Runs the two-sided RDMA sweeps and writes:
  experiments/rdma_two_sided_clients.csv
  experiments/rdma_two_sided_ratio.csv
  experiments/rdma_two_sided_zipf.csv
  experiments/rdma_two_sided_valsize.csv

Options:
  --host HOST          Server private-link IP. Defaults to $SERVER_IP.
  --port PORT          Server port. Default: 9091.
  --device NAME        RDMA device. Default: mlx5_0.
  --clients LIST       Client-count sweep values. Default: "1 2 4 8".
  --extra-clients N    Client count for ratio/Zipf/value-size sweeps. Default: 4.
  --ratios LIST        GET-ratio sweep values. Default: "0.5 0.75 0.90 0.95 1.0".
  --zipfs LIST         Zipf sweep values. Default: "0.0 0.5 0.8 1.0 1.5".
  --value-sizes LIST   Value-size sweep values. Default: "32 64 128 512".
  --ops N              Timed operations per run. Default: 10000.
  --keys N             Key count. Default: 1024.
  --value-size N       Default value size for non-value-size sweeps. Default: 64.
  --get-ratio R        Default GET fraction for non-ratio sweeps. Default: 0.95.
  --zipf-s S           Default Zipf skew for non-Zipf sweeps. Default: 0.8.
  --warmup N           Warmup operations per run. Default: 1000.
  --outdir DIR         Output directory. Default: experiments.
  --build-dir DIR      Build directory. Default: build.
  --only-clients       Run only the client-count sweep.
  --reset              Delete this script's CSVs before running.
  --dry-run            Print commands without running them.
  -h, --help           Show this help text.
EOF
}

die() {
  echo "run_two_sided_rdma_experiments: $*" >&2
  exit 2
}

print_cmd() {
  printf '  '
  printf '%q ' "$@"
  printf '\n'
}

run_cmd() {
  print_cmd "$@"
  if [[ "$DRY_RUN" -eq 0 ]]; then
    "$@"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) HOST="${2:-}"; shift 2 ;;
    --port) PORT="${2:-}"; shift 2 ;;
    --device) DEVICE="${2:-}"; shift 2 ;;
    --clients) CLIENTS="${2:-}"; shift 2 ;;
    --extra-clients) EXTRA_CLIENTS="${2:-}"; shift 2 ;;
    --ratios) RATIOS="${2:-}"; shift 2 ;;
    --zipfs) ZIPFS="${2:-}"; shift 2 ;;
    --value-sizes) VALUE_SIZES="${2:-}"; shift 2 ;;
    --ops) OPS="${2:-}"; shift 2 ;;
    --keys) KEYS="${2:-}"; shift 2 ;;
    --value-size) VALUE_SIZE="${2:-}"; shift 2 ;;
    --get-ratio) GET_RATIO="${2:-}"; shift 2 ;;
    --zipf-s) ZIPF_S="${2:-}"; shift 2 ;;
    --warmup) WARMUP="${2:-}"; shift 2 ;;
    --outdir) OUTDIR="${2:-}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    --only-clients) ONLY_CLIENTS=1; shift ;;
    --reset) RESET=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

[[ -n "$HOST" ]] || die "pass --host SERVER_IP or set SERVER_IP"
[[ -n "$CLIENTS" ]] || die "--clients cannot be empty"

CLIENT="$BUILD_DIR/kv_client_rdma"
if [[ "$DRY_RUN" -eq 0 && ! -x "$CLIENT" ]]; then
  die "missing executable: $CLIENT"
fi

mkdir -p "$OUTDIR"
CLIENTS_CSV="${CLIENTS_CSV:-$OUTDIR/rdma_two_sided_clients.csv}"
RATIO_CSV="${RATIO_CSV:-$OUTDIR/rdma_two_sided_ratio.csv}"
ZIPF_CSV="${ZIPF_CSV:-$OUTDIR/rdma_two_sided_zipf.csv}"
VALSIZE_CSV="${VALSIZE_CSV:-$OUTDIR/rdma_two_sided_valsize.csv}"

if [[ "$RESET" -eq 1 ]]; then
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "dry-run: would remove $CLIENTS_CSV $RATIO_CSV $ZIPF_CSV $VALSIZE_CSV"
  else
    rm -f "$CLIENTS_CSV" "$RATIO_CSV" "$ZIPF_CSV" "$VALSIZE_CSV"
  fi
fi

echo "Two-sided RDMA sweeps"
echo "  host=$HOST port=$PORT device=$DEVICE outdir=$OUTDIR"

for C in ${CLIENTS//,/ }; do
  echo
  echo "== Two-sided RDMA clients: $C =="
  run_cmd "$CLIENT" \
    --host "$HOST" --port "$PORT" \
    --mode two-sided --device "$DEVICE" \
    --benchmark \
    --clients "$C" --ops "$OPS" --keys "$KEYS" \
    --value-size "$VALUE_SIZE" --get-ratio "$GET_RATIO" \
    --zipf-s "$ZIPF_S" --warmup "$WARMUP" \
    --csv "$CLIENTS_CSV"
done

if [[ "$ONLY_CLIENTS" -eq 0 ]]; then
  for R in ${RATIOS//,/ }; do
    echo
    echo "== Two-sided RDMA get-ratio: $R =="
    run_cmd "$CLIENT" \
      --host "$HOST" --port "$PORT" \
      --mode two-sided --device "$DEVICE" \
      --benchmark \
      --clients "$EXTRA_CLIENTS" --ops "$OPS" --keys "$KEYS" \
      --value-size "$VALUE_SIZE" --get-ratio "$R" \
      --zipf-s "$ZIPF_S" --warmup "$WARMUP" \
      --csv "$RATIO_CSV"
  done

  for S in ${ZIPFS//,/ }; do
    echo
    echo "== Two-sided RDMA zipf-s: $S =="
    run_cmd "$CLIENT" \
      --host "$HOST" --port "$PORT" \
      --mode two-sided --device "$DEVICE" \
      --benchmark \
      --clients "$EXTRA_CLIENTS" --ops "$OPS" --keys "$KEYS" \
      --value-size "$VALUE_SIZE" --get-ratio "$GET_RATIO" \
      --zipf-s "$S" --warmup "$WARMUP" \
      --csv "$ZIPF_CSV"
  done

  for V in ${VALUE_SIZES//,/ }; do
    echo
    echo "== Two-sided RDMA value-size: $V =="
    run_cmd "$CLIENT" \
      --host "$HOST" --port "$PORT" \
      --mode two-sided --device "$DEVICE" \
      --benchmark \
      --clients "$EXTRA_CLIENTS" --ops "$OPS" --keys "$KEYS" \
      --value-size "$V" --get-ratio "$GET_RATIO" \
      --zipf-s "$ZIPF_S" --warmup "$WARMUP" \
      --csv "$VALSIZE_CSV"
  done
fi

echo
echo "Two-sided RDMA experiments complete:"
echo "  $CLIENTS_CSV"
if [[ "$ONLY_CLIENTS" -eq 0 ]]; then
  echo "  $RATIO_CSV"
  echo "  $ZIPF_CSV"
  echo "  $VALSIZE_CSV"
fi
