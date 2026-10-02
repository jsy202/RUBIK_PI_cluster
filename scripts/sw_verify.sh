#!/usr/bin/env bash
# Post-project software verification (host only, no RUBIK Pi hardware).
# Runs inside the Dockerfile.verify image with the repository mounted read-only at /src:
#
#   docker build -f Dockerfile.verify -t rubik-verify .
#   docker run --rm --security-opt seccomp=unconfined -v "$PWD":/src:ro -v "$PWD/.verify-out":/out \
#       rubik-verify bash /src/scripts/sw_verify.sh <stage>
#
# seccomp=unconfined is only needed so that `setarch -R` (ASLR off) is allowed for the sanitizer and
# fuzzer binaries: the GCC 9 / clang 10 ASan runtimes intermittently crash during their own start-up
# (before main) on kernels with high mmap ASLR entropy (observed: 8/30 runs on Linux 6.8, 0/30 with -R).
#
# Stages:
#   tests        build + run QtTest (parser, MainWindow) and pytest
#   coverage     same tests built with --coverage; gcovr (C++) + coverage.py (Python) summaries
#   sanitize     tests built with ASan + UBSan; known-defect tests run separately to record reports
#   fuzz [sec]   libFuzzer + ASan + UBSan on rpmparser.h (default 60 s)
#   mutate       mutation testing on a temporary copy (scripts/mutation_test.py)
#   negctl       negative controls: deliberate defects in a temporary copy must make the tests,
#                the fuzz oracles and ASan fail
#   all          tests coverage sanitize fuzz mutate negctl
# Results are written to $OUT (default /out). /src is never modified.
set -euo pipefail
SRC=${SRC:-/src}
OUT=${OUT:-/out}
JOBS=$(nproc)
export QT_QPA_PLATFORM=offscreen
NORAND=(setarch "$(uname -m)" -R)
mkdir -p "$OUT"

# known-defect tests (QEXPECT_FAIL) that execute undefined behaviour on purpose (DEF-SW-02)
KNOWN_UB_TESTS="knownDefect_animationDurationOverflow"

qbuild() {   # qbuild <builddir> <pro> [qmake args...]
    local dir=$1 pro=$2; shift 2
    mkdir -p "$dir" && (cd "$dir" && qmake "$pro" "$@" >/dev/null && make -j"$JOBS" >/dev/null)
}

stage_tests() {
    qbuild /b/tests/parser "$SRC/tests/tst_rpmparser/tst_rpmparser.pro"
    qbuild /b/tests/mw "$SRC/tests/tst_mainwindow/tst_mainwindow.pro"
    local rc=0
    /b/tests/parser/tst_rpmparser -o "$OUT/qttest_parser.txt,txt" -o -,txt || rc=1
    (cd /tmp && /b/tests/mw/tst_mainwindow -o "$OUT/qttest_mainwindow.txt,txt" -o -,txt) || rc=1
    (cd "$SRC" && python3 -m pytest -q -p no:cacheprovider 2>&1 | tee "$OUT/pytest.txt") || rc=1
    return $rc
}

stage_coverage() {
    local cf=(CONFIG+=debug "QMAKE_CXXFLAGS+=--coverage" "QMAKE_LFLAGS+=--coverage")
    qbuild /b/cov/parser "$SRC/tests/tst_rpmparser/tst_rpmparser.pro" "${cf[@]}"
    qbuild /b/cov/mw "$SRC/tests/tst_mainwindow/tst_mainwindow.pro" "${cf[@]}"
    /b/cov/parser/tst_rpmparser >/dev/null
    (cd /tmp && /b/cov/mw/tst_mainwindow >/dev/null 2>&1)
    # Scope: application sources written for this project. Excluded: Qt headers, system headers,
    # moc_/ui_ generated files, test code. Exception-only (throw) and unreachable branches excluded.
    local scope=(-r "$SRC" --object-directory /b/cov
                 -f "$SRC/rpmparser.h" -f "$SRC/mainwindow.cpp" -f "$SRC/mainwindow.h" -f "$SRC/timestamplabel.h"
                 --exclude-throw-branches --exclude-unreachable-branches)
    mkdir -p "$OUT/html"
    (cd /b/cov && gcovr "${scope[@]}" --json-pretty -o "$OUT/coverage_cpp.json" \
        --json-summary-pretty --json-summary "$OUT/coverage_cpp_summary.json" \
        --txt "$OUT/coverage_cpp.txt" --html-details "$OUT/html/coverage.html" -s) | tee "$OUT/coverage_cpp_console.txt"
    python3 "$SRC/scripts/coverage_modules.py" "$OUT/coverage_cpp.json" "$SRC" | tee "$OUT/coverage_modules.md"
    (cd "$SRC" && COVERAGE_FILE=/tmp/.coverage python3 -m pytest -q -p no:cacheprovider \
        --cov=analyze_latency --cov=validate_latency --cov-branch --cov-report=term-missing 2>&1 | tee "$OUT/coverage_python.txt")
}

stage_sanitize() {
    local san="-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer"
    local sf=(CONFIG+=debug "QMAKE_CXXFLAGS+=$san" "QMAKE_LFLAGS+=$san")
    qbuild /b/san/parser "$SRC/tests/tst_rpmparser/tst_rpmparser.pro" "${sf[@]}"
    qbuild /b/san/mw "$SRC/tests/tst_mainwindow/tst_mainwindow.pro" "${sf[@]}"
    # Leak detection off: Qt/fontconfig keep process-lifetime allocations that LSan reports as leaks.
    export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1
    export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
    local rc=0
    "${NORAND[@]}" /b/san/parser/tst_rpmparser 2>&1 | tee "$OUT/sanitize_parser.txt" | tail -1 || rc=1
    (cd /tmp && RUBIK_SKIP_KNOWN_UB=1 "${NORAND[@]}" /b/san/mw/tst_mainwindow 2>&1) \
        | tee "$OUT/sanitize_mainwindow.txt" | tail -1 || rc=1
    grep -c "runtime error\|ERROR: AddressSanitizer" "$OUT"/sanitize_parser.txt "$OUT"/sanitize_mainwindow.txt || true
    # Known defects: record the sanitizer reports they trigger (expected), without halting.
    UBSAN_OPTIONS=print_stacktrace=0:halt_on_error=0 \
        bash -c "cd /tmp && ${NORAND[*]} /b/san/mw/tst_mainwindow $KNOWN_UB_TESTS" > "$OUT/sanitize_known_defects.txt" 2>&1 || true
    grep "runtime error" "$OUT/sanitize_known_defects.txt" || true
    return $rc
}

stage_fuzz() {
    local secs=${1:-60}
    local qt=(-I/usr/include/x86_64-linux-gnu/qt5 -I/usr/include/x86_64-linux-gnu/qt5/QtCore)
    mkdir -p /b/fuzz "$OUT/fuzz_artifacts"
    clang++-10 -std=c++17 -g -O1 -fPIC -fsanitize=fuzzer,address,undefined,float-cast-overflow \
        -fno-sanitize-recover=all "${qt[@]}" -I"$SRC" "$SRC/tests/fuzz/fuzz_rpmparser.cpp" \
        -lQt5Core -o /b/fuzz/fuzz_rpmparser
    # working corpus grows; the committed seeds stay as they are. -len_control=0: without it libFuzzer
    # raises the input-length limit slowly and inputs > 8 KiB (buffer cap path) were not generated
    # within 120 s (negative control cap_removed, first run).
    cp -r "$SRC/tests/fuzz/corpus" /b/fuzz/corpus
    local rc=0
    ASAN_OPTIONS=detect_leaks=1 "${NORAND[@]}" /b/fuzz/fuzz_rpmparser /b/fuzz/corpus -max_len=12000 -len_control=0 \
        -max_total_time="$secs" -print_final_stats=1 -artifact_prefix="$OUT/fuzz_artifacts/" \
        > "$OUT/fuzz_${secs}s.txt" 2>&1 || rc=$?
    grep -E "^(#[0-9]+ +DONE|stat::|Done|==|ORACLE|SUMMARY)" "$OUT/fuzz_${secs}s.txt" || true
    echo "fuzz exit code: $rc" | tee -a "$OUT/fuzz_${secs}s.txt"
    return "$rc"
}

stage_mutate() {
    python3 "$SRC/scripts/mutation_test.py" --src "$SRC" --out "$OUT"
}

fuzz_negctl() {   # fuzz_negctl <name> <sed expression on rpmparser.h> <max seconds> [seed to leave out]
    local name=$1 expr=$2 secs=$3 skip=${4:-} d=/b/negctl/$1
    mkdir -p "$d/src/tests/fuzz" && cp "$SRC/rpmparser.h" "$d/src/" && cp "$SRC/tests/fuzz/fuzz_rpmparser.cpp" "$d/src/tests/fuzz/"
    sed -i "$expr" "$d/src/rpmparser.h"
    if cmp -s "$SRC/rpmparser.h" "$d/src/rpmparser.h"; then echo "$name: sed did not change the file" >&2; return 1; fi
    clang++-10 -std=c++17 -g -O1 -fPIC -fsanitize=fuzzer,address,undefined,float-cast-overflow -fno-sanitize-recover=all \
        -I/usr/include/x86_64-linux-gnu/qt5 -I/usr/include/x86_64-linux-gnu/qt5/QtCore -I"$d/src" \
        "$d/src/tests/fuzz/fuzz_rpmparser.cpp" -lQt5Core -o "$d/fuzz"
    mkdir -p "$d/corpus" && cp "$SRC"/tests/fuzz/corpus/* "$d/corpus/"
    [ -z "$skip" ] || rm "$d/corpus/$skip"
    local rc=0 t0=$SECONDS
    ASAN_OPTIONS=detect_leaks=0 "${NORAND[@]}" "$d/fuzz" "$d/corpus" -max_len=12000 -len_control=0 -max_total_time="$secs" \
        -artifact_prefix="$d/" > "$OUT/negctl_fuzz_$name.txt" 2>&1 || rc=$?
    { echo "--- injected: sed '$expr' rpmparser.h"; diff "$SRC/rpmparser.h" "$d/src/rpmparser.h" || true
      echo "--- fuzzer exit code: $rc after $((SECONDS - t0)) s (limit $secs s)"; } >> "$OUT/negctl_fuzz_$name.txt"
    echo "$name: exit=$rc time=$((SECONDS - t0))s $(grep -m1 -oE 'ORACLE VIOLATION: .*|ERROR: AddressSanitizer: [a-z-]+|runtime error: .*' "$OUT/negctl_fuzz_$name.txt")"
}

stage_negctl() {
    # 1-3: test suites against deliberate production defects (same harness as mutation testing)
    python3 "$SRC/scripts/mutation_test.py" --src "$SRC" --out "$OUT" --only M07,M04,M19 --results negctl_tests.json
    # 4: fuzz oracles must catch a removed buffer cap: with the > 8 KiB seed, and without it
    #    (the fuzzer has to generate a long enough input itself)
    fuzz_negctl cap_removed 's/buffer.remove(0, buffer.size() - maxBufferBytes);/;/' 120
    fuzz_negctl cap_removed_noseed 's/buffer.remove(0, buffer.size() - maxBufferBytes);/;/' 120 overflow_9k
    # 5: ASan must catch an out-of-bounds read in the parser (line copied from 70 bytes before '\n')
    fuzz_negctl oob_read 's/QByteArray line = buffer.left(nl);/QByteArray line(buffer.constData() + nl - 70, 70);/' 120
}

case "${1:-all}" in
    tests) stage_tests ;;
    coverage) stage_coverage ;;
    sanitize) stage_sanitize ;;
    fuzz) stage_fuzz "${2:-60}" ;;
    mutate) stage_mutate ;;
    negctl) stage_negctl ;;
    all) stage_tests && stage_coverage && stage_sanitize && stage_fuzz "${2:-60}" && stage_mutate && stage_negctl ;;
    *) echo "unknown stage: $1" >&2; exit 2 ;;
esac
