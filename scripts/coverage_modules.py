#!/usr/bin/env python3
"""Per-module C++ coverage from a gcovr JSON report (scripts/sw_verify.sh coverage).

A source line belongs to the function with the greatest start line <= that line in the same file
(so the closing lines of the constructor after its paint lambda are counted with the lambda).
Branch numbers are gcov arcs after gcovr's --exclude-throw-branches/--exclude-unreachable-branches.

Usage: coverage_modules.py coverage_cpp.json [src_root]
"""
import json
import sys

MODULES = [
    ("RPM parser / input validation", "rpmparser.h", ["RpmParser::consume", "RpmParser::correct"]),
    ("Input sources (serial, replay, simulation)", "mainwindow.cpp",
     ["MainWindow::readSerialData", "MainWindow::setupSerialPort", "MainWindow::setupReplay",
      "MainWindow::feedReplaySample", "MainWindow::setupSimulation", "MainWindow::generateSimulatedRpm"]),
    ("Pending sample + 30 Hz UI update", "mainwindow.cpp",
     ["MainWindow::acceptRpmSample", "MainWindow::flushPendingRpm", "MainWindow::setDisplayRpm",
      "MainWindow::displayRpm"]),
    ("State / data conversion (speed, RPM correction)", "mainwindow.cpp",
     ["MainWindow::update_values", "MainWindow::correctRpmValue"]),
    ("Measurement / CSV logging", None,
     ["MainWindow::MainWindow(QWidget*)::{lambda", "MainWindow::logEvent", "TimestampLabel::paintEvent"]),
    ("Lifecycle (constructor setup, destructor)", "mainwindow.cpp",
     ["MainWindow::MainWindow(QWidget*)", "MainWindow::~MainWindow"]),
]


def owner(functions, line):
    best = None
    for f in functions:
        if f["lineno"] <= line and (best is None or f["lineno"] > best["lineno"]):
            best = f
    return best["name"] if best else "?"


def module_of(file, fname):
    for title, mfile, prefixes in MODULES:
        if mfile and not file.endswith(mfile):
            continue
        for p in prefixes:
            # exact match for the constructor so its lambda is not swallowed
            if fname.startswith(p) and not (p.endswith("(QWidget*)") and fname != p):
                return title
    return None


def pct(a, b):
    return f"{100.0 * a / b:.1f}% ({a}/{b})" if b else "– (0/0)"


def main():
    data = json.load(open(sys.argv[1]))
    stats = {t: dict(l=0, lc=0, f=0, fc=0, b=0, bc=0) for t, _, _ in MODULES}
    unmapped = []
    seen_fn = set()
    for file in data["files"]:
        fns = file["functions"]
        for f in fns:
            m = module_of(file["file"], f["name"])
            if m is None:
                unmapped.append(f["name"])
                continue
            key = (file["file"], f["name"])
            if key in seen_fn:
                continue
            seen_fn.add(key)
            stats[m]["f"] += 1
            stats[m]["fc"] += f["execution_count"] > 0
        for ln in file["lines"]:
            if ln.get("gcovr/noncode"):
                continue
            m = module_of(file["file"], owner(fns, ln["line_number"]))
            if m is None:
                continue
            s = stats[m]
            s["l"] += 1
            s["lc"] += ln["count"] > 0
            for br in ln["branches"]:
                s["b"] += 1
                s["bc"] += br["count"] > 0
    print("| Module | Line | Function | Branch |")
    print("|---|---:|---:|---:|")
    tot = dict(l=0, lc=0, f=0, fc=0, b=0, bc=0)
    for t, _, _ in MODULES:
        s = stats[t]
        for k in tot:
            tot[k] += s[k]
        print(f"| {t} | {pct(s['lc'], s['l'])} | {pct(s['fc'], s['f'])} | {pct(s['bc'], s['b'])} |")
    print(f"| **Total (in scope)** | {pct(tot['lc'], tot['l'])} | {pct(tot['fc'], tot['f'])} | {pct(tot['bc'], tot['b'])} |")
    if unmapped:
        print("\nunmapped functions:", ", ".join(sorted(set(unmapped))))


if __name__ == "__main__":
    main()
