#!/usr/bin/env python3
"""PASS/FAIL validation of a latency CSV against the 30 Hz UI soft budget.

Reuses the CSV reader and percentile of scripts/analyze_latency.py (unchanged).
Jitter keeps the existing definition: population standard deviation
(Python statistics.pstdev / MATLAB std(x, 1)).

Measured quantity = e2e_latency_ms = t_frame - t_in (see validation/measurement_boundary.md):
from the Qt app accepting a parsed sample to the end of rpmLabel's paintEvent.
It does NOT include serial transmission or physical display.

Criterion (REQ-PERF-001): p<percentile>(e2e_latency_ms) <= budget_ms  (default p95 <= 33.3 ms).
Budget exceedances above the budget are reported but are not by themselves a FAIL
(soft budget, not a hard real-time deadline).

Usage:
    python3 scripts/validate_latency.py latency_metrics_*.csv [--budget-ms 33.3] [--percentile 95] [--json out.json]
Exit code: 0 PASS, 1 FAIL, 2 input error.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_latency import percentile, read_latencies  # noqa: E402


def coalesced_samples(seqs: list[int]) -> int:
    """Samples accepted by the app but never painted/logged (overwritten before the 30 Hz flush,
    or repeated values that caused no repaint): gaps in the logged seq numbers."""
    return sum(max(0, b - a - 1) for a, b in zip(seqs, seqs[1:]))


def summarize(rows: list[dict], budget_ms: float = 33.3, pct: float = 95.0) -> dict:
    lat = [float(r["e2e_latency_ms"]) for r in rows]
    seqs = [int(r["seq"]) for r in rows]
    exceed = [v for v in lat if v > budget_ms]
    accepted = (seqs[-1] - seqs[0] + 1) if seqs else 0
    stats = {
        "samples_logged": len(lat),
        "seq_first": seqs[0],
        "seq_last": seqs[-1],
        "samples_accepted_in_range": accepted,
        "samples_not_logged": coalesced_samples(seqs),
        "mean_ms": statistics.mean(lat),
        "median_ms": statistics.median(lat),
        "p95_ms": percentile(lat, 95),
        "p99_ms": percentile(lat, 99),
        "min_ms": min(lat),
        "max_ms": max(lat),
        "jitter_stddev_ms": statistics.pstdev(lat),
        "budget_ms": budget_ms,
        "budget_exceed_count": len(exceed),
        "budget_exceed_ratio": len(exceed) / len(lat),
        "criterion": f"p{pct:g} <= {budget_ms:g} ms",
        "criterion_value_ms": percentile(lat, pct),
    }
    stats["verdict"] = "PASS" if stats["criterion_value_ms"] <= budget_ms else "FAIL"
    return stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_file", type=Path)
    ap.add_argument("--budget-ms", type=float, default=33.3)
    ap.add_argument("--percentile", type=float, default=95.0)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args(argv)
    try:
        rows = read_latencies(args.csv_file)
    except SystemExit as e:
        print(e, file=sys.stderr)
        return 2
    s = summarize(rows, args.budget_ms, args.percentile)
    for k, v in s.items():
        print(f"{k}: {v:.3f}" if isinstance(v, float) else f"{k}: {v}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(s, indent=2))
    return 0 if s["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
