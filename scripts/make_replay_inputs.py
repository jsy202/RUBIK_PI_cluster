#!/usr/bin/env python3
"""Generate deterministic RPM replay files (one integer per line) for CLUSTER_REPLAY_FILE runs."""
import sys
from pathlib import Path

out = Path(sys.argv[1] if len(sys.argv) > 1 else "validation/perf_inputs")
out.mkdir(parents=True, exist_ok=True)
seq = {
    "fixed_2500.txt": [2500] * 1500,                                  # 30 s @ 20 ms, constant value
    "rapid_0_8000.txt": [0 if i % 2 == 0 else 8000 for i in range(1500)],   # 30 s @ 20 ms, alternating extremes
    "ramp_33ms.txt": [1000 + i for i in range(900)],                  # 30 s @ 33 ms, every value distinct
    "burst_1ms.txt": [1000 + i for i in range(30000)],                # 30 s @ 1 ms, 1 kHz input
}
for name, values in seq.items():
    (out / name).write_text("\n".join(map(str, values)) + "\n")
    print(name, len(values))
