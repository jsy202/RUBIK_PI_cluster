"""The end-to-end fault-injection runner must be able to inject an exact number of faults, so one
real-time window (32,400 frames = 10.8 min at 20 ms) can contain exactly one fault."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "validation" / "fault_injection"))
import inject_e2e  # noqa: E402


def test_exact_fault_count_is_injected():
    faults = inject_e2e.make_faults(32400, rate=0.0, seed=101, count=1)
    assert len(faults) == 1


def test_exact_count_is_reproducible_and_depends_on_seed():
    a = inject_e2e.make_faults(32400, 0.0, 101, count=1)
    b = inject_e2e.make_faults(32400, 0.0, 101, count=1)
    c = inject_e2e.make_faults(32400, 0.0, 102, count=1)
    assert a == b
    assert a != c


def test_exact_count_faults_stay_away_from_the_edges():
    for seed in range(50):
        (frame,) = inject_e2e.make_faults(1000, 0.0, seed, count=1)
        assert 2 <= frame <= 1000 - 3


def test_rate_mode_is_unchanged():
    faults = inject_e2e.make_faults(10000, 0.01, 20261123)
    assert 60 < len(faults) < 150

