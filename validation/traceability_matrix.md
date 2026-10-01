# Traceability Matrix — RUBIK Pi Qt Digital Cluster

| REQ | Production code (validation 브랜치) | Test / 측정 | 결과 | Commit |
|---|---|---|---|---|
| IN-001 | `rpmparser.h` `RpmParser::consume` (기존 `readSerialData` 루프를 그대로 옮김), `mainwindow.cpp` `readSerialData` | TC-01, 02, 04, 08, 10, 11, 12 | PASS | `116b4df`, `84c8be5` |
| IN-002 | 같은 코드 (`toInt` 실패 → skip) | TC-05, 06, 07 | PASS | 〃 |
| IN-003 | 같은 코드 (미종결 꼬리는 버퍼에 유지) | TC-09, 14 | PASS | 〃 |
| IN-004 | 같은 코드 (`maxLineLen`, `maxBufferBytes`) | TC-13, 15 | PASS | 〃 |
| IN-005 | `RpmParser::correct` ← `correctRpmValue` | TC-03 | PASS | 〃 |
| IN-006 | 추출 전후 동치 | TC-16 (차등), 변이 2종으로 negative control | PASS. 변이는 각각 1 failed로 검출. `.trimmed()` 제거는 등가 변이라 검출 불가 | `84c8be5` |
| UI-001 | `kUiIntervalMs = 1000/30` (mainwindow.h), `flushPendingRpm` | PS-01~05의 `samples_not_logged` | 관측됨 (`performance_report.md`) | – (기존) |
| PERF-001 | 측정 코드는 기존 그대로: `acceptRpmSample`의 `t_in`, `TimestampLabel::painted`의 `t_frame` | `scripts/validate_latency.py`, TC-17~22, PS-01~05 | 시나리오별 PASS/FAIL (`performance_report.md`) | `84c8be5` |
| PERF-002 | `validate_latency.coalesced_samples` | TC-21, PS-01~05 | PASS | `84c8be5` |
| MEAS-001 | `CLUSTER_SIMULATE`(기존), `CLUSTER_REPLAY_FILE`(`setupReplay` L250, `feedReplaySample` L274) | PS-01~05 실행 | 실행됨 | `0a77c37` |
