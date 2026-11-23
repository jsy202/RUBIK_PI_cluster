#!/usr/bin/env bash
# Hardware-free test run: QtTest suites (framecheck, parser, MainWindow on offscreen Qt) and a short
# parser-level fault-injection simulation. Meant to run inside the rubik-verify image with the
# repository mounted read-only at /src:
#   docker run --rm -v "$PWD":/src:ro rubik-verify bash /src/scripts/run_unit_tests.sh
set -euo pipefail
SRC=${SRC:-/src}
OUT=${OUT:-/tmp/unit}
QT_INC=/usr/include/x86_64-linux-gnu/qt5

for t in tst_framecheck tst_rpmparser; do
    mkdir -p "$OUT/$t" && cd "$OUT/$t"
    qmake "$SRC/tests/$t/$t.pro" >/dev/null && make -j2 >/dev/null
    "./$t"
done

mkdir -p "$OUT/tst_mainwindow" && cd "$OUT/tst_mainwindow"
qmake "$SRC/tests/tst_mainwindow/tst_mainwindow.pro" >/dev/null && make -j2 >/dev/null
QT_QPA_PLATFORM=offscreen ./tst_mainwindow

cd "$OUT"
g++ -O2 -std=c++17 -fPIC -I"$SRC" -I"$QT_INC" -I"$QT_INC/QtCore" \
    "$SRC/validation/fault_injection/parser_sim.cpp" -o parser_sim -lQt5Core
./parser_sim 900000 32400 20261123
