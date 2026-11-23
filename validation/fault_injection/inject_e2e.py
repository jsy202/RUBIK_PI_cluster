#!/usr/bin/env python3
"""End-to-end fault injection against the real application (offscreen Qt, pseudo-terminal).

A PTY is not a physical UART and offscreen Qt is not a display: this checks what the application
does with corrupted lines (parser -> 30 Hz throttle -> label -> CSV), not the electrical link.

The injection rate is accelerated (default 1 fault per 100 frames) because real time is 20 ms per
frame; the reference rate (1 per 32,400 frames) is evaluated by parser_sim.cpp.

usage: inject_e2e.py --binary ./digital_cluster --stage plain|protect --out DIR
                     [--frames 10000] [--rate 0.01] [--seed 20261123] [--period-ms 20]
"""
import argparse
import csv
import glob
import json
import math
import os
import pty
import random
import subprocess
import sys
import time
import tty


def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def make_line(value: int, protect: bool) -> bytes:
    text = str(value).encode()
    if protect:
        return text + b"," + b"%02X" % crc8(text) + b"\n"
    return text + b"\n"


def make_truth(frames: int, seed: int):
    rng = random.Random(seed)
    return [int(2500 + 1700 * math.sin(i * 0.02 * 1.3)) + rng.randint(-60, 60) for i in range(frames)]


def make_faults(frames: int, rate: float, seed: int, count=None):
    """Faults keyed by frame index. With `count`, exactly that many faults are placed at random
    frames (rate is ignored); otherwise each frame gets one with probability `rate`."""
    rng = random.Random(seed + 1)

    def one():
        return {"type": rng.randrange(3), "pos": rng.random(), "bit": rng.randrange(8),
                "byte": rng.randrange(256)}

    if count is not None:
        return {i: one() for i in sorted(rng.sample(range(2, frames - 2), count))}
    faults = {}
    for i in range(2, frames - 2):
        if rng.random() < rate:
            faults[i] = one()
    return faults


def corrupt(line: bytes, f) -> bytes:
    b = bytearray(line)
    if f["type"] == 0:
        p = int(f["pos"] * len(b)); b[p] ^= 1 << f["bit"]
    elif f["type"] == 1:
        del b[int(f["pos"] * len(b))]
    else:
        b.insert(int(f["pos"] * (len(b) + 1)), f["byte"])
    return bytes(b)


def fit_offset(rows, truth, period):
    """Offset (ms) between the application's clock and the first sent frame."""
    sample = rows[: min(len(rows), 250)]
    best, best_score = 0, -1
    for off in range(0, 6000, 2):
        score = 0
        for t_in, raw in sample:
            idx = int((t_in - off) / period + 0.5)
            if 0 <= idx < len(truth) and truth[idx] == raw:
                score += 1
        if score > best_score:
            best, best_score = off, score
    return best, best_score / max(1, len(sample))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--stage", choices=["plain", "protect"], required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--frames", type=int, default=10000)
    ap.add_argument("--rate", type=float, default=0.01)
    ap.add_argument("--faults", type=int, default=None, help="inject exactly this many faults (overrides --rate)")
    ap.add_argument("--seed", type=int, default=20261123)
    ap.add_argument("--period-ms", type=float, default=20.0)
    a = ap.parse_args()
    protect = a.stage == "protect"
    os.makedirs(a.out, exist_ok=True)

    truth = make_truth(a.frames, a.seed)
    faults = make_faults(a.frames, a.rate, a.seed, a.faults)

    master, slave = pty.openpty()
    tty.setraw(slave)
    slave_name = os.ttyname(slave)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", CLUSTER_SERIAL_PORT=slave_name, CLUSTER_BAUD="9600")
    if protect:
        env["CLUSTER_PROTECT"] = "1"
    app = subprocess.Popen([os.path.abspath(a.binary)], cwd=a.out, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3.0)                                    # let the window come up and the port open

    t0 = time.perf_counter()
    injections = []
    for i in range(a.frames):
        line = make_line(truth[i], protect)
        if i in faults:
            bad = corrupt(line, faults[i])
            injections.append({"frame": i, "type": ["bitflip", "delete", "insert"][faults[i]["type"]],
                               "original_hex": line.hex(), "sent_hex": bad.hex()})
            line = bad
        target = t0 + i * a.period_ms / 1000.0
        while time.perf_counter() < target:
            time.sleep(0.0005)
        os.write(master, line)
    time.sleep(1.0)
    app.terminate()
    try:
        app.wait(timeout=5)
    except subprocess.TimeoutExpired:
        app.kill()

    csvs = sorted(glob.glob(os.path.join(a.out, "latency_metrics_*.csv")))
    rows = []
    with open(csvs[-1]) as fh:
        for r in csv.DictReader(fh):
            rows.append((float(r["t_in_ms"]), int(r["raw_rpm"])))
    rejects = []
    for p in sorted(glob.glob(os.path.join(a.out, "reject_log_*.csv"))):
        with open(p) as fh:
            rejects += list(csv.DictReader(fh))

    # Align the application's clock to the sender's clock using the many clean frames.
    off, match = fit_offset(rows, truth, a.period_ms)
    wrong = []
    for t_in, raw in rows:
        idx = int((t_in - off) / a.period_ms + 0.5)
        near = [truth[j] for j in range(idx - 1, idx + 2) if 0 <= j < len(truth)]
        if raw not in near:
            wrong.append({"t_in_ms": t_in, "frame_est": idx, "shown_raw_rpm": raw,
                          "true_rpm_here": truth[idx] if 0 <= idx < len(truth) else None})

    reasons = {}
    for r in rejects:
        reasons[r["reason"]] = reasons.get(r["reason"], 0) + 1
    summary = {
        "stage": a.stage, "frames": a.frames, "period_ms": a.period_ms, "fault_rate_per_frame": a.rate,
        "seed": a.seed, "exact_faults": a.faults, "injected": len(injections),
        "painted_rows": len(rows), "alignment_match_ratio": round(match, 3),
        "wrong_values_painted": len(wrong), "reject_log_rows": len(rejects), "reject_reasons": reasons,
    }
    with open(os.path.join(a.out, "summary.json"), "w") as fh:
        json.dump({"summary": summary, "wrong_painted": wrong[:50], "injections": injections}, fh, indent=1)
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if match > 0.8 else 3


if __name__ == "__main__":
    sys.exit(main())
