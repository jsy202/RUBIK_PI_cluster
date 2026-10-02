#!/usr/bin/env python3
"""Hand-selected mutation testing for the RUBIK cluster application logic (post-project verification).

Each mutant is one textual replacement in a production file. The repository is copied to a temporary
directory first; mutants are applied ONLY to that copy, one at a time, and the file is restored
before the next mutant. The source tree passed with --src is never written (sw_verify.sh mounts it
read-only), and the script checks at the end that the copy's production files are byte-identical
to the originals.

Kill criterion: any test suite (QtTest parser, QtTest MainWindow, pytest) exits non-zero or times
out. A mutant that does not compile is "invalid". Mutants reviewed as behaviour-preserving are
marked equivalent=True up front; they are run anyway and reported separately (not in the score).

Usage: mutation_test.py --src /src --out /out [--only M07,M12] [--results name.json]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

# (id, file, original, replacement, simulated fault, equivalent?)
MUTANTS = [
    # --- rpmparser.h: parsing / input validation / buffering ---
    ("M01", "rpmparser.h", "if (buffer.size() > maxBufferBytes)", "if (buffer.size() >= maxBufferBytes)",
     "buffer cap comparison > -> >=", True),
    ("M02", "rpmparser.h", "        buffer.remove(0, buffer.size() - maxBufferBytes);\n", "        ;\n",
     "buffer cap (8 KiB) removed: unbounded buffer", False),
    ("M03", "rpmparser.h", "buffer.remove(0, buffer.size() - maxBufferBytes);", "buffer.truncate(maxBufferBytes);",
     "overflow policy: drop newest instead of oldest bytes", False),
    ("M04", "rpmparser.h", "if (line.size() > maxLineLen) continue;", "if (line.size() >= maxLineLen) continue;",
     "line-length boundary > -> >= (64-byte line rejected)", False),
    ("M05", "rpmparser.h", "        if (line.size() > maxLineLen) continue;\n", "",
     "line-length check removed", False),
    ("M06", "rpmparser.h", "if (!ok) continue;", "if (ok) continue;",
     "parse success/failure condition inverted", False),
    ("M07", "rpmparser.h", "        if (!ok) continue;\n", "",
     "invalid-input rejection removed (failed parse appended as 0)", False),
    ("M08", "rpmparser.h", "buffer.remove(0, nl + 1);", "buffer.remove(0, nl);",
     "off-by-one: line terminator not consumed", False),
    ("M09", "rpmparser.h", "QString::fromLatin1(line.trimmed())", "QString::fromLatin1(line)",
     "whitespace trimming removed", True),
    ("M10", "rpmparser.h", "return rawRpm < 0 ? 0 : rawRpm;", "return rawRpm <= 0 ? 0 : rawRpm;",
     "negative clamp boundary < -> <=", True),
    ("M11", "rpmparser.h", "return rawRpm < 0 ? 0 : rawRpm;", "return rawRpm;",
     "negative RPM clamp removed", False),
    # --- mainwindow.cpp: pending sample / 30 Hz update / conversion / measurement ---
    ("M12", "mainwindow.cpp", "    if (m_pendingRpm < 0) return;\n", "",
     "flush without pending sample not ignored", False),
    ("M13", "mainwindow.cpp", "    m_pendingRpm = finalRpm;\n", "",
     "accepted sample value not stored (update skipped)", False),
    ("M14", "mainwindow.cpp", "m_displayRpm = rpm;", ";",
     "displayed-state update skipped", False),
    ("M15", "mainwindow.cpp", "if (m_frameSeq <= 0 || m_frameSeq == m_lastLoggedSeq)", "if (m_frameSeq <= 0)",
     "per-frame log de-duplication removed", False),
    ("M16", "mainwindow.cpp", "    if (delta < kSmallDelta) {", "    if (delta <= kSmallDelta) {",
     "animation threshold boundary < -> <=", False),
    ("M17", "mainwindow.cpp", "std::clamp(delta * 2, 60, 180)", "std::clamp(delta, 60, 180)",
     "animation duration conversion changed", False),
    ("M18", "mainwindow.cpp", "currentRpm * (4.5 / 2.5)", "currentRpm * (2.5 / 4.5)",
     "speed conversion ratio inverted", False),
    ("M19", "mainwindow.cpp", "qint64 dt_sp = (t_paint - t_serial);", "qint64 dt_sp = (t_serial - t_paint);",
     "latency sign inverted in CSV", False),
    ("M20", "mainwindow.cpp", "    m_seq++;\n", "",
     "sample sequence number not incremented", False),
    ("M21", "mainwindow.cpp", "if (m_replayIndex >= m_replaySamples.size())", "if (m_replayIndex > m_replaySamples.size())",
     "replay end-of-input boundary >= -> >", False),
    # --- scripts/validate_latency.py: measurement analysis ---
    ("P01", "scripts/validate_latency.py", 'stats["criterion_value_ms"] <= budget_ms', 'stats["criterion_value_ms"] < budget_ms',
     "PASS/FAIL verdict boundary <= -> <", False),
    ("P02", "scripts/validate_latency.py", "max(0, b - a - 1)", "max(0, b - a)",
     "not-logged sample count off by one", False),
    ("P03", "scripts/analyze_latency.py", "rank = (len(ordered) - 1) * pct / 100.0", "rank = len(ordered) * pct / 100.0",
     "percentile rank formula changed", False),
]

PRODUCTION = sorted({m[1] for m in MUTANTS})
TIMEOUT_S = 60


def sh(cmd, cwd, timeout=None, env=None):
    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True, timeout=timeout,
                           env={**os.environ, **(env or {})})
        return r.returncode, r.stdout + r.stderr, time.time() - t0
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return "timeout", out, time.time() - t0


def build(work, bdir):
    for name, pro in (("parser", "tests/tst_rpmparser/tst_rpmparser.pro"),
                      ("mw", "tests/tst_mainwindow/tst_mainwindow.pro")):
        d = os.path.join(bdir, name)
        os.makedirs(d, exist_ok=True)
        if not os.path.exists(os.path.join(d, "Makefile")):
            rc, out, _ = sh(f"qmake {work}/{pro}", d)
            if rc != 0:
                return False, out
        rc, out, _ = sh(f"make -j{os.cpu_count()}", d)
        if rc != 0:
            return False, out
    return True, ""


def run_suites(work, bdir, tmp):
    """Returns (killed, reason, failing test names, log)."""
    suites = [
        ("qttest-parser", f"{bdir}/parser/tst_rpmparser"),
        ("qttest-mainwindow", f"{bdir}/mw/tst_mainwindow"),
        ("pytest", f"python3 -m pytest -q -p no:cacheprovider {work}/tests/analysis"),
    ]
    failing, log, killed, reason = [], "", False, ""
    for name, cmd in suites:
        rc, out, _ = sh(cmd, tmp if name != "pytest" else work, timeout=TIMEOUT_S,
                        env={"QT_QPA_PLATFORM": "offscreen"})
        log += f"--- {name} rc={rc}\n{out}\n"
        if rc == "timeout":
            killed, reason = True, reason or f"{name}: timeout ({TIMEOUT_S}s)"
            failing.append(f"{name}: TIMEOUT")
        elif rc != 0:
            killed, reason = True, reason or f"{name}: exit {rc}"
            for line in out.splitlines():
                if line.startswith("FAIL!") or line.startswith("XPASS") or line.startswith("FAILED "):
                    failing.append(line.split("Compared")[0].split(" - ")[0].strip())
            if not any(f.startswith(("FAIL!", "XPASS", "FAILED")) for f in failing):
                failing.append(f"{name}: exit {rc} (crash/abort)")
    return killed, reason, failing, log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="/src")
    ap.add_argument("--out", default="/out")
    ap.add_argument("--only", default="")
    ap.add_argument("--results", default="mutation_results.json")
    args = ap.parse_args()
    only = set(filter(None, args.only.split(",")))

    tmp = tempfile.mkdtemp(prefix="mut-")
    work = os.path.join(tmp, "src")
    shutil.copytree(args.src, work, ignore=shutil.ignore_patterns(".git", ".verify-out", "perf_results"))
    bdir = os.path.join(tmp, "build")
    logdir = os.path.join(args.out, "mutation_logs")
    os.makedirs(logdir, exist_ok=True)

    ok, out = build(work, bdir)
    if not ok:
        print(out)
        sys.exit("baseline build failed")
    killed, reason, failing, log = run_suites(work, bdir, tmp)
    if killed:
        print(log)
        sys.exit(f"baseline (unmutated) suites fail: {reason}")
    print("baseline: all suites pass")

    results = []
    for mid, rel, orig, repl, desc, equivalent in MUTANTS:
        if only and mid not in only:
            continue
        path = os.path.join(work, rel)
        text = open(path, newline="").read()   # keep CRLF/LF exactly (mainwindow.cpp has both)
        n = text.count(orig)
        if n != 1:
            results.append(dict(id=mid, file=rel, fault=desc, status="invalid",
                                detail=f"pattern found {n} times", tests=[]))
            print(f"{mid} INVALID (pattern x{n})")
            continue
        open(path, "w", newline="").write(text.replace(orig, repl))
        try:
            if rel.endswith((".h", ".cpp")):
                ok, out = build(work, bdir)
            else:
                ok, out = True, ""
            if not ok:
                status, detail, tests, log = "invalid", "does not compile", [], out
            else:
                k, detail, tests, log = run_suites(work, bdir, tmp)
                status = "killed" if k else "survived"
        finally:
            open(path, "w", newline="").write(text)
            if rel.endswith((".h", ".cpp")):
                build(work, bdir)   # binaries must match the restored source before the next mutant
        if equivalent:
            status = f"equivalent ({status})"
        results.append(dict(id=mid, file=rel, original=orig.strip(), mutant=repl.strip() or "<deleted>",
                            fault=desc, status=status, detail=detail, tests=sorted(set(tests))))
        with open(os.path.join(logdir, f"{mid}.log"), "w") as f:
            f.write(log)
        print(f"{mid} {status:24s} {desc}  <- {', '.join(sorted(set(tests)))[:150]}")

    # restore check: production files in the copy equal the originals
    for rel in PRODUCTION:
        a = open(os.path.join(args.src, rel), "rb").read()
        b = open(os.path.join(work, rel), "rb").read()
        assert a == b, f"{rel} not restored"

    real = [r for r in results if not r["status"].startswith(("equivalent", "invalid"))]
    k = sum(r["status"] == "killed" for r in real)
    summary = dict(total=len(results), killed=k, survived=len(real) - k,
                   equivalent=sum(r["status"].startswith("equivalent") for r in results),
                   invalid=sum(r["status"] == "invalid" for r in results),
                   score=f"{k}/{len(real)} = {100.0 * k / len(real):.1f}%" if real else "n/a")
    print(json.dumps(summary))
    json.dump(dict(summary=summary, mutants=results), open(os.path.join(args.out, args.results), "w"),
              indent=2)
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
