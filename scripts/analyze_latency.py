#!/usr/bin/env python3
"""Analyze Qt digital-cluster E2E latency logs.

Input CSV columns are produced by the application:
seq,source,t_in_ms,t_frame_ms,e2e_latency_ms,raw_rpm,rpm
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from pathlib import Path


def read_latencies(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit(f"No rows found in {path}")
    required = {"seq", "t_in_ms", "t_frame_ms", "e2e_latency_ms", "rpm"}
    missing = required.difference(rows[0])
    if missing:
        raise SystemExit(f"Missing columns in {path}: {', '.join(sorted(missing))}")
    return rows


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    rank = (len(ordered) - 1) * pct / 100.0
    lo = math.floor(rank)
    hi = math.ceil(rank)
    if lo == hi:
        return ordered[lo]
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo)


def write_summary(path: Path, stats: dict[str, float]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["metric", "value"])
        for key, value in stats.items():
            writer.writerow([key, f"{value:.6f}" if isinstance(value, float) else value])


def write_plots(out_dir: Path, seq: list[int], latencies: list[float]) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; skipped PNG plot generation.")
        return

    out_dir.mkdir(parents=True, exist_ok=True)

    plt.figure(figsize=(10, 4))
    plt.plot(seq, latencies, linewidth=0.8, label="latency")
    plt.axhline(statistics.mean(latencies), color="tab:orange", linewidth=1.2, label="mean")
    plt.xlabel("Sample")
    plt.ylabel("Latency (ms)")
    plt.title("E2E Latency Trend")
    plt.grid(True, alpha=0.25)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / "latency_trend.png", dpi=180)
    plt.close()

    plt.figure(figsize=(6, 4))
    plt.hist(latencies, bins=40, density=True, color="tab:blue", alpha=0.75)
    plt.xlabel("Latency (ms)")
    plt.ylabel("Probability Density")
    plt.title("E2E Latency Distribution")
    plt.grid(True, alpha=0.25)
    plt.tight_layout()
    plt.savefig(out_dir / "latency_distribution.png", dpi=180)
    plt.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_file", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("analysis_output"))
    args = parser.parse_args()

    rows = read_latencies(args.csv_file)
    seq = [int(row["seq"]) for row in rows]
    latencies = [float(row["e2e_latency_ms"]) for row in rows]

    stats = {
        "samples": len(latencies),
        "mean_ms": statistics.mean(latencies),
        "jitter_stddev_ms": statistics.pstdev(latencies),
        "min_ms": min(latencies),
        "max_ms": max(latencies),
        "p50_ms": percentile(latencies, 50),
        "p95_ms": percentile(latencies, 95),
        "p99_ms": percentile(latencies, 99),
    }

    args.out_dir.mkdir(parents=True, exist_ok=True)
    write_summary(args.out_dir / "latency_summary.csv", stats)
    write_plots(args.out_dir, seq, latencies)

    print(f"samples: {stats['samples']}")
    print(f"mean: {stats['mean_ms']:.2f} ms")
    print(f"jitter/stddev: {stats['jitter_stddev_ms']:.2f} ms")
    print(f"worst: {stats['max_ms']:.2f} ms")
    print(f"summary: {args.out_dir / 'latency_summary.csv'}")


if __name__ == "__main__":
    main()
