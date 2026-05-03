#!/usr/bin/env bash
# Shared optional CPU sampling helpers for experiment runner scripts.
#
# Runner scripts source this file and provide:
#   CPU_SSH, CPU_REMOTE_DIR, CPU_CSV, CPU_INTERVAL, SERVER_PID,
#   SERVER_PROCESS, DRY_RUN, OUTDIR

cpu_enabled() {
  [[ -n "${CPU_SSH:-}" ]]
}

cpu_resolve_server_pid() {
  if [[ -n "${SERVER_PID:-}" ]]; then
    printf '%s\n' "$SERVER_PID"
    return 0
  fi

  local process_name="${SERVER_PROCESS:-}"
  [[ -n "$process_name" ]] || {
    echo "CPU sampling needs SERVER_PROCESS or --server-pid" >&2
    return 2
  }

  local quoted_process
  quoted_process="$(printf '%q' "$process_name")"
  ssh "$CPU_SSH" "pgrep -n -x $quoted_process"
}

cpu_start_sampler() {
  local label="$1"
  local transport="$2"
  local clients="$3"
  local metadata="$4"

  if ! cpu_enabled; then
    return 0
  fi

  if [[ "${DRY_RUN:-0}" -eq 1 ]]; then
    echo "dry-run: would start server CPU sampler for label=$label clients=$clients metadata=$metadata" >&2
    return 0
  fi

  local pid
  if ! pid="$(cpu_resolve_server_pid)"; then
    echo "failed to find server PID on $CPU_SSH; pass --server-pid if pgrep is ambiguous" >&2
    return 2
  fi

  local metadata_flag=""
  if [[ "$metadata" -eq 1 ]]; then
    metadata_flag="--metadata"
  fi

  local remote_dir_q csv_q csv_dir_q label_q transport_q interval_q log_q
  remote_dir_q="$(printf '%q' "$CPU_REMOTE_DIR")"
  csv_q="$(printf '%q' "$CPU_CSV")"
  csv_dir_q="$(printf '%q' "$(dirname "$CPU_CSV")")"
  label_q="$(printf '%q' "$label")"
  transport_q="$(printf '%q' "$transport")"
  interval_q="$(printf '%q' "$CPU_INTERVAL")"
  log_q="$(printf '%q' "/tmp/cs2640_cpu_${transport}_${clients}_${metadata}_$$.log")"

  local remote_cmd
  remote_cmd="cd $remote_dir_q && mkdir -p $csv_dir_q && nohup python3 scripts/measure_cpu.py --pid $pid --label $label_q --transport $transport_q --clients $clients $metadata_flag --csv $csv_q --interval $interval_q > $log_q 2>&1 < /dev/null & echo \$!"

  local sampler_pid
  sampler_pid="$(ssh "$CPU_SSH" "$remote_cmd")"
  echo "CPU sampler started on $CPU_SSH: server_pid=$pid sampler_pid=$sampler_pid label=$label clients=$clients" >&2
  sleep 0.3
  printf '%s\n' "$sampler_pid"
}

cpu_stop_sampler() {
  local sampler_pid="$1"
  if ! cpu_enabled || [[ -z "$sampler_pid" ]]; then
    return 0
  fi

  if [[ "${DRY_RUN:-0}" -eq 1 ]]; then
    echo "dry-run: would stop CPU sampler $sampler_pid on $CPU_SSH" >&2
    return 0
  fi

  local sampler_pid_q
  sampler_pid_q="$(printf '%q' "$sampler_pid")"
  ssh "$CPU_SSH" "kill -INT $sampler_pid_q 2>/dev/null || true; i=0; while kill -0 $sampler_pid_q 2>/dev/null && [ \$i -lt 50 ]; do sleep 0.1; i=\$((i + 1)); done; kill -TERM $sampler_pid_q 2>/dev/null || true"
}

run_with_optional_cpu() {
  local label="$1"
  local transport="$2"
  local clients="$3"
  local metadata="$4"
  shift 4

  local sampler_pid=""
  if cpu_enabled; then
    sampler_pid="$(cpu_start_sampler "$label" "$transport" "$clients" "$metadata")"
  fi

  set +e
  run_cmd "$@"
  local rc=$?
  set -e

  if cpu_enabled; then
    cpu_stop_sampler "$sampler_pid"
  fi

  return "$rc"
}

cpu_sync_csv() {
  local outdir="$1"
  if ! cpu_enabled; then
    return 0
  fi

  mkdir -p "$outdir"
  local local_path="$outdir/$(basename "$CPU_CSV")"
  local remote_path="$CPU_SSH:$CPU_REMOTE_DIR/$CPU_CSV"

  if [[ "${DRY_RUN:-0}" -eq 1 ]]; then
    echo "dry-run: would copy $remote_path to $local_path"
    return 0
  fi

  if scp -q "$remote_path" "$local_path"; then
    echo "CPU CSV synced to $local_path"
  else
    echo "warning: failed to sync CPU CSV from $remote_path" >&2
  fi
}
