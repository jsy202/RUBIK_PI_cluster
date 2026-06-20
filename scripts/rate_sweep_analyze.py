#!/usr/bin/env python3
"""Aggregate rate-sweep runs into raw_results.csv (per run) and summary.csv (per rate).

input_samples    = lines in the replay file (all fed if the app logged "Replay finished")
accepted_samples = input_samples when the replay finished (replay path calls acceptRpmSample for every value);
                   otherwise the highest logged seq (lower bound) and replay_finished=False
logged_samples   = CSV rows (painted + logged)
dropped          = accepted - logged     (overwritten before the 30 Hz flush, i.e. never painted/logged)
actual_input_hz  = (seq_last - seq_first) / (t_in_last - t_in_first) from the CSV (rate as seen by the app)
Latency statistics are over logged samples only (boundary: sample acceptance -> paintEvent).
"""
import csv, json, statistics, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_latency import percentile, read_latencies  # noqa: E402

runs_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "validation/rate_sweep/runs")
out_dir = runs_dir.parent
rows = []
for d in sorted(runs_dir.iterdir()):
    hz, ms, n, rep = (open(d / "condition.txt").read().split())
    data = read_latencies(next(d.glob("latency_metrics_*.csv")))
    lat = [float(r["e2e_latency_ms"]) for r in data]
    seq = [int(r["seq"]) for r in data]
    tin = [int(r["t_in_ms"]) for r in data]
    finished = "Replay finished" in (d / "app.log").read_text(errors="replace")
    accepted = int(n) if finished else seq[-1]
    rows.append({
        "nominal_hz": int(hz), "interval_ms": int(ms), "rep": int(rep), "replay_finished": finished,
        "input_samples": int(n), "accepted_samples": accepted, "logged_samples": len(lat),
        "dropped": accepted - len(lat), "drop_ratio": round((accepted - len(lat)) / accepted, 4),
        "actual_input_hz": round((seq[-1] - seq[0]) / ((tin[-1] - tin[0]) / 1000.0), 1) if tin[-1] > tin[0] else "",
        "mean_ms": round(statistics.mean(lat), 2), "p50_ms": percentile(lat, 50), "p95_ms": percentile(lat, 95),
        "p99_ms": percentile(lat, 99), "max_ms": max(lat), "exceed_33_3ms": sum(v > 33.3 for v in lat),
        "run_dir": d.name,
    })
rows.sort(key=lambda r: (r["nominal_hz"], r["rep"]))
with open(out_dir / "raw_results.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

summary = []
for hz in sorted({r["nominal_hz"] for r in rows}):
    g = [r for r in rows if r["nominal_hz"] == hz]
    def agg(k): vals = [r[k] for r in g]; return round(statistics.mean(vals), 3), min(vals), max(vals)
    s = {"nominal_hz": hz, "interval_ms": g[0]["interval_ms"], "reps": len(g),
         "all_replays_finished": all(r["replay_finished"] for r in g),
         "actual_input_hz_mean": round(statistics.mean(r["actual_input_hz"] for r in g if r["actual_input_hz"] != ""), 1)}
    for k in ("input_samples", "accepted_samples", "logged_samples", "dropped", "drop_ratio", "mean_ms",
              "p50_ms", "p95_ms", "p99_ms", "max_ms", "exceed_33_3ms"):
        m, lo, hi = agg(k); s[f"{k}_mean"] = m; s[f"{k}_min"] = lo; s[f"{k}_max"] = hi
    summary.append(s)
with open(out_dir / "summary.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(summary[0])); w.writeheader(); w.writerows(summary)
print(json.dumps(summary, indent=1))
