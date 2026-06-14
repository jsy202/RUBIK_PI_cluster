"""Tests for the hardware-free analysis path (scripts/analyze_latency.py, scripts/validate_latency.py)."""

import csv
import statistics
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import analyze_latency  # noqa: E402
import validate_latency  # noqa: E402

HEADER = ["seq", "source", "t_in_ms", "t_frame_ms", "e2e_latency_ms", "raw_rpm", "rpm"]


def write_csv(path, rows):
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(HEADER)
        for seq, lat in rows:
            w.writerow([seq, "simulation", 100 * seq, 100 * seq + lat, lat, 2500, 2500])
    return path


def test_percentile_matches_linear_interpolation():
    assert analyze_latency.percentile([1, 2, 3, 4], 50) == 2.5
    assert analyze_latency.percentile([10], 95) == 10


def test_existing_jitter_definition_is_population_stddev(tmp_path):
    p = write_csv(tmp_path / "a.csv", [(1, 10), (2, 20), (3, 30)])
    s = validate_latency.summarize(analyze_latency.read_latencies(p))
    assert s["jitter_stddev_ms"] == pytest.approx(statistics.pstdev([10, 20, 30]))


def test_summary_statistics_and_budget(tmp_path):
    rows = [(i, 10) for i in range(1, 96)] + [(i, 40) for i in range(96, 101)]   # 5 % above budget
    s = validate_latency.summarize(analyze_latency.read_latencies(write_csv(tmp_path / "b.csv", rows)))
    assert s["samples_logged"] == 100 and s["median_ms"] == 10 and s["max_ms"] == 40
    assert s["budget_exceed_count"] == 5 and s["budget_exceed_ratio"] == 0.05
    assert s["verdict"] == "PASS"            # p95 = 10 + 0.05*(40-10) = 11.5 <= 33.3


def test_verdict_fails_when_p95_exceeds_budget(tmp_path):
    rows = [(i, 10) for i in range(1, 81)] + [(i, 50) for i in range(81, 101)]
    s = validate_latency.summarize(analyze_latency.read_latencies(write_csv(tmp_path / "c.csv", rows)))
    assert s["criterion_value_ms"] > 33.3 and s["verdict"] == "FAIL"


def test_seq_gaps_count_samples_not_logged(tmp_path):
    s = validate_latency.summarize(analyze_latency.read_latencies(
        write_csv(tmp_path / "d.csv", [(1, 5), (2, 5), (5, 5), (9, 5)])))
    assert s["samples_not_logged"] == 2 + 3
    assert s["samples_accepted_in_range"] == 9


def test_cli_exit_codes(tmp_path):
    ok = write_csv(tmp_path / "ok.csv", [(1, 5), (2, 6)])
    bad = write_csv(tmp_path / "bad.csv", [(1, 90), (2, 95)])
    run = lambda p: subprocess.run([sys.executable, str(ROOT / "scripts" / "validate_latency.py"), str(p)],
                                   capture_output=True, text=True).returncode
    assert run(ok) == 0 and run(bad) == 1


def test_missing_columns_is_input_error(tmp_path):
    p = tmp_path / "m.csv"
    p.write_text("seq,foo\n1,2\n")
    assert validate_latency.main([str(p)]) == 2


def test_existing_analyze_script_runs_on_valid_csv(tmp_path):
    p = write_csv(tmp_path / "e.csv", [(1, 5), (2, 7), (3, 9)])
    r = subprocess.run([sys.executable, str(ROOT / "scripts" / "analyze_latency.py"), str(p), "--out-dir", str(tmp_path / "out")],
                       capture_output=True, text=True)
    assert r.returncode == 0 and "mean: 7.00 ms" in r.stdout
    assert (tmp_path / "out" / "latency_summary.csv").exists()
