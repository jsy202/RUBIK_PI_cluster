# Test Cases — RUBIK Pi Qt Digital Cluster

## QtTest — `tests/tst_rpmparser` (Docker: Ubuntu 20.04, Qt 5.12.8, GCC 9.3)

| TC | 테스트 | 입력 | 기대 | REQ |
|---|---|---|---|---|
| TC-01 | `normalValue` | `2500\n` | [2500], 버퍼 비움 | IN-001 |
| TC-02 | `zero` | `0\n` | [0] | IN-001 |
| TC-03 | `negativeIsParsedThenClampedToZero` | `-120\n` | 파싱 −120, `correct` → 0, 7000은 그대로 | IN-005 |
| TC-04 | `intBoundaries` | INT_MAX, INT_MIN | 둘 다 수용 | IN-001 |
| TC-05 | `overflowIsSkipped` | 2147483648, 99999999999 | 버림, 다음 값은 처리 | IN-002 |
| TC-06 | `invalidStringsAreSkipped` | abc, 12a, 1.5, 0x10 | 버림 | IN-002 |
| TC-07 | `emptyAndWhitespaceLinesAreSkipped` | 빈 줄, 공백 줄 | 버림 | IN-002 |
| TC-08 | `crlfAndSurroundingSpacesAreTolerated` | `\r\n`, 앞뒤 공백, 탭 | 수용 | IN-001 |
| TC-09 | `splitFrameAcrossReads` | "25" / "00\n26" / "00\n" | 2500, 2600 | IN-003 |
| TC-10 | `multipleFramesInOneRead` | 3줄 | 순서 유지 | IN-001 |
| TC-11 | `repeatedValuesAreAllReturned` | 2500 ×3 | 3개 모두 반환(표시 단계에서 같은 값은 repaint 없음 — 측정 문서 참조) | IN-001 |
| TC-12 | `rapidChangesKeepOrder` | 0/8000 교대 | 순서 유지 | IN-001 |
| TC-13 | `lineAtMaxLengthIsAcceptedLongerIsSkipped` | 64바이트, 65바이트 | 64는 수용, 65는 버림 | IN-004 |
| TC-14 | `unterminatedTailIsKeptForNextRead` | "1000\n20" | [1000], 버퍼 "20" | IN-003 |
| TC-15 | `bufferOverflowDropsOldestBytes` | 8 KiB 'x' + "1234\n" | 버림(경계에 걸친 줄 손상), 다음 줄부터 회복 | IN-004 |
| TC-16 | `extractedParserMatchesBaselineInlineCode` | 16개 청크 | 출력과 버퍼가 baseline 루프 복사본과 같음 | IN-006 |

## pytest — `tests/analysis`

| TC | 테스트 | REQ |
|---|---|---|
| TC-17 | `test_percentile_matches_linear_interpolation` | PERF-001 |
| TC-18 | `test_existing_jitter_definition_is_population_stddev` | (기존 jitter 정의 유지) |
| TC-19 | `test_summary_statistics_and_budget` | PERF-001 |
| TC-20 | `test_verdict_fails_when_p95_exceeds_budget` | PERF-001 |
| TC-21 | `test_seq_gaps_count_samples_not_logged` | PERF-002 |
| TC-22 | `test_cli_exit_codes` (PASS 0 / FAIL 1) | PERF-001 |
| TC-23 | `test_missing_columns_is_input_error` (2) | – |
| TC-24 | `test_existing_analyze_script_runs_on_valid_csv` | (기존 스크립트 보존) |

## 성능 회귀 시나리오 — `scripts/run_perf_scenarios.sh` (테스트가 아니라 측정)

| ID | 입력 | 의도 |
|---|---|---|
| PS-01 `simulation_50hz` | 기존 `CLUSTER_SIMULATE` (20 ms, 사인파 + 잡음) | 기존 시뮬레이션 경로 |
| PS-02 `fixed_2500_20ms` | 2500 고정, 20 ms | 같은 값 → repaint/로그 없음 확인 |
| PS-03 `rapid_0_8000_20ms` | 0/8000 교대, 20 ms | 급변 + 30 Hz 샘플링 |
| PS-04 `ramp_33ms` | 1씩 증가, 33 ms | 입력 주기 ≈ UI 주기 |
| PS-05 `burst_1ms` | 1씩 증가, 1 ms (1 kHz) | 과부하 입력, 덮어쓰기 |

## 미실행

- 실제 RUBIK Pi 3 + Arduino + 센서 실행(하드웨어 없음)
- 실제 디스플레이(X11/Wayland) 실행
- `CLUSTER_ANIMATE=1` 측정
