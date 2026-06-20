#!/bin/bash
# Input-rate sweep (Post-project Validation, NOT hardware validation).
# Runs the existing app (Dockerfile.test image, Qt offscreen) with CLUSTER_REPLAY_FILE at several
# input rates. Every replayed value is distinct (1000, 1001, ...) so each displayed sample repaints.
# Each replay file holds SECONDS worth of samples; the run gets SECONDS+PAD so the replay can finish.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-validation/rate_sweep/runs}
SECONDS_OF_INPUT=${SECONDS_OF_INPUT:-15}
PAD=${PAD:-10}
REPS=${REPS:-3}
RATES=${RATES:-"10:100 30:33 60:17 100:10 200:5 500:2 1000:1"}   # nominal_hz:interval_ms
mkdir -p "$OUT" validation/rate_sweep/inputs
docker build -q -f Dockerfile.test -t rubik-cluster-test:validation . >/dev/null
for spec in $RATES; do
  hz=${spec%%:*}; ms=${spec##*:}
  n=$(( SECONDS_OF_INPUT * 1000 / ms ))
  inp=validation/rate_sweep/inputs/ramp_${ms}ms_${n}.txt
  python3 -c "import sys; open('$inp','w').write(''.join(f'{1000+i}\n' for i in range($n)))"
  for r in $(seq 1 "$REPS"); do
    d="$OUT/${hz}hz_${ms}ms_rep${r}"; rm -rf "$d"; mkdir -p "$d"
    docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/$d:/out" -v "$PWD/validation/rate_sweep/inputs:/inputs:ro" -w /out \
      -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp \
      -e CLUSTER_REPLAY_FILE=/inputs/$(basename "$inp") -e CLUSTER_REPLAY_INTERVAL_MS="$ms" \
      rubik-cluster-test:validation bash -c "timeout -s INT $((SECONDS_OF_INPUT + PAD)) /build/app/digital_cluster > app.log 2>&1 || true"
    echo "$hz $ms $n $r" > "$d/condition.txt"
    echo "done ${hz}Hz (${ms} ms) rep ${r}: $(wc -l < "$d"/latency_metrics_*.csv) csv lines, finished=$(grep -c 'Replay finished' "$d/app.log" || true)"
  done
done
