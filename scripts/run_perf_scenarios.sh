#!/bin/bash
# Repeatable performance runs WITHOUT RUBIK Pi / Arduino hardware.
# Builds the app in Docker (Dockerfile.test), runs it on the Qt "offscreen" platform for each
# scenario, collects the app's own latency CSV and validates it with scripts/validate_latency.py.
# Results are Post-project Validation results for THIS machine; they are not RUBIK Pi results.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-validation/perf_results}
DUR=${DUR:-30}
mkdir -p "$OUT"
python3 scripts/make_replay_inputs.py validation/perf_inputs >/dev/null
docker build -q -f Dockerfile.test -t rubik-cluster-test:validation . >/dev/null

run() {  # name, env...
  local name=$1; shift
  rm -rf "$OUT/$name"; mkdir -p "$OUT/$name"
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/$OUT/$name:/out" -v "$PWD/validation/perf_inputs:/inputs:ro" \
    -w /out -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp "$@" rubik-cluster-test:validation \
    bash -c "timeout -s INT $((DUR + 2)) /build/app/digital_cluster > app.log 2>&1 || true"
  local csv; csv=$(ls "$OUT/$name"/latency_metrics_*.csv)
  python3 scripts/validate_latency.py "$csv" --json "$OUT/$name/summary.json" > "$OUT/$name/summary.txt" || true
  echo "== $name: $(grep -E '^(samples_logged|samples_not_logged|p95_ms|max_ms|budget_exceed_count|verdict):' "$OUT/$name/summary.txt" | tr '\n' ' ')"
}

run simulation_50hz   -e CLUSTER_SIMULATE=1
run fixed_2500_20ms   -e CLUSTER_REPLAY_FILE=/inputs/fixed_2500.txt   -e CLUSTER_REPLAY_INTERVAL_MS=20
run rapid_0_8000_20ms -e CLUSTER_REPLAY_FILE=/inputs/rapid_0_8000.txt -e CLUSTER_REPLAY_INTERVAL_MS=20
run ramp_33ms         -e CLUSTER_REPLAY_FILE=/inputs/ramp_33ms.txt    -e CLUSTER_REPLAY_INTERVAL_MS=33
run burst_1ms         -e CLUSTER_REPLAY_FILE=/inputs/burst_1ms.txt    -e CLUSTER_REPLAY_INTERVAL_MS=1
