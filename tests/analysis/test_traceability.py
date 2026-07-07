"""Keeps validation/software_verification/requirements_traceability.md honest:
every test it names exists in the test sources, and every SW requirement row names at least one test.
(Hardware-dependent rows are allowed to have none.)"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MATRIX = ROOT / "validation" / "software_verification" / "requirements_traceability.md"
SOURCES = {
    "tst_rpmparser": (ROOT / "tests/tst_rpmparser/tst_rpmparser.cpp", r"^\s+void {}\(\)$"),
    "tst_mainwindow": (ROOT / "tests/tst_mainwindow/tst_mainwindow.cpp", r"^\s+void {}\(\)$"),
    "test_latency_analysis": (ROOT / "tests/analysis/test_latency_analysis.py", r"^def {}\("),
}


def rows():
    for line in MATRIX.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\| \*\*(REQ-[A-Z]+-\d+)\*\*", line)
        if m:
            yield m.group(1), line


def test_every_named_test_exists():
    names = set()
    for _, line in rows():
        names |= set(re.findall(r"`(tst_\w+|test_latency_analysis)::(\w+)`", line))
    assert names
    for suite, name in names:
        path, pattern = SOURCES[suite]
        assert re.search(pattern.format(re.escape(name)), path.read_text(), re.M), f"{suite}::{name} not found"


def test_every_sw_requirement_is_linked_to_a_test():
    reqs = list(rows())
    sw = [(rid, line) for rid, line in reqs if not rid.startswith("REQ-HW-")]
    assert len(sw) == 17
    for rid, line in sw:
        cells = re.split(r"(?<!\\)\|", line)          # escaped pipes (\|) inside cells are not separators
        assert re.search(r"`(tst_\w+|test_latency_analysis)::\w+`", cells[3]), f"{rid} has no test"
    hw = [line for rid, line in reqs if rid.startswith("REQ-HW-")]
    assert hw and all("Hardware-dependent / Not revalidated" in line for line in hw)
