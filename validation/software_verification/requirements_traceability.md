# Requirements Traceability — Post-project Software Verification

> 기존 실제 하드웨어 연구 이후, host 기반 환경(개발 PC + Docker, Ubuntu 20.04, Qt 5.12.8, offscreen)에서
> SW 입력 처리와 회귀시험의 검증 범위를 coverage, mutation, fuzz testing으로 확장한 결과다.
> RUBIK Pi 3에서 다시 실행한 결과가 아니다.

ID 체계는 기존 [`validation/requirements.md`](../requirements.md)를 그대로 잇는다. 기존 ID(IN-001~006, UI-001, PERF-001/002, MEAS-001)는 의미를 바꾸지 않았다. 이번에 추가한 ID는 IN-007, UI-002~005, MEAS-002/003, HW-001/002다.
모든 요구사항은 **현재 코드가 실제로 하는 동작**에서 도출했다. 이상적인 사양을 새로 만든 것이 아니다.

Result 열의 의미:
- **PASS**: 연결된 테스트가 모두 통과한다.
- **PASS — DEF-SW-xx 수정됨**: 이 검증에서 결함을 재현했고(`sw-hardening-findings` 태그, `f9e211c`), 이후 수정해서 지금은 테스트가 PASS다.
- **PASS + Known Measurement Limitation**: 정상 동작은 통과하지만, 측정 의미에 관한 재현된 한계가 있다. 연구 당시 측정 의미와 얽혀 있어 production 로직을 바꾸지 않았다(`QEXPECT_FAIL`로 고정).
- **Hardware-dependent / Not revalidated**: 하드웨어가 있어야 검증할 수 있다. 이번 작업에서 다시 검증하지 않았다.

테스트 이름 표기: `tst_rpmparser::…`, `tst_mainwindow::…`는 QtTest 함수, `test_latency_analysis::…`는 pytest 함수다. 이 표에 적힌 모든 테스트 이름이 실제 테스트 코드에 존재하는지는 `tests/analysis/test_traceability.py`가 확인한다(CI에서 실행).

## Traceability Matrix

| Requirement | Production Code | Test Case | Evidence | Result |
|---|---|---|---|---|
| **REQ-IN-001** 시리얼 입력은 `\n`으로 끝나는 줄마다 10진 정수 1개다. 줄 전체가 `[ASCII 공백][+\|-]숫자 1개 이상[ASCII 공백]` 형식이어야 한다(앞자리 0 허용) | `rpmparser.h` `RpmParser::parseLine`, `RpmParser::consume`; `mainwindow.cpp` `readSerialData` | `tst_rpmparser::normalValue`, `tst_rpmparser::zero`, `tst_rpmparser::intBoundaries`, `tst_rpmparser::crlfAndSurroundingSpacesAreTolerated`, `tst_rpmparser::multipleFramesInOneRead`, `tst_rpmparser::repeatedValuesAreAllReturned`, `tst_rpmparser::rapidChangesKeepOrder`, `tst_rpmparser::robustnessInputClasses`, `tst_mainwindow::serialPathViaPseudoTerminal`, `tst_mainwindow::replayUsesSerialLineProtocol` | QtTest log, mutation M06/M07 killed | PASS |
| **REQ-IN-002** 정수가 아니거나, int 범위를 벗어나거나, 빈 줄이면 그 줄만 버리고 다음 줄을 계속 처리한다 | `RpmParser::parseLine` (형식 검사 후 변환) | `tst_rpmparser::overflowIsSkipped`, `tst_rpmparser::invalidStringsAreSkipped`, `tst_rpmparser::emptyAndWhitespaceLinesAreSkipped`, `tst_rpmparser::robustnessInputClasses`, `tst_rpmparser::nulAcrossReadBoundaryIsRejected`, `tst_mainwindow::serialPathViaPseudoTerminal` | QtTest log, mutation M06/M07/N01–N04, fuzz oracle O6 | PASS — **DEF-SW-04 수정됨** (수정 전: NUL 4행 XFAIL) |
| **REQ-IN-003** 한 줄이 여러 번의 read에 나뉘어 와도 하나의 값으로 조립하고, 끝나지 않은 꼬리는 다음 read까지 버퍼에 남긴다 | `RpmParser::consume` (persistent buffer) | `tst_rpmparser::splitFrameAcrossReads`, `tst_rpmparser::unterminatedTailIsKeptForNextRead`, `tst_mainwindow::serialPathViaPseudoTerminal` | QtTest log; fuzz oracle O4(분할 불변성) | PASS |
| **REQ-IN-004** 64바이트를 넘는 줄은 버린다. 버퍼가 8 KiB를 넘으면 가장 오래된 바이트부터 버린다 | `RpmParser::consume` L25–26, L33; `mainwindow.h` `kMaxLineLen`, `kMaxBufferBytes` | `tst_rpmparser::lineAtMaxLengthIsAcceptedLongerIsSkipped`, `tst_rpmparser::bufferOverflowDropsOldestBytes`, `tst_rpmparser::robustnessInputClasses` | mutation M02–M05 killed; fuzz oracle O1; negative control `cap_removed` | PASS |
| **REQ-IN-005** 음수 RPM은 표시할 때 0으로 보정하고, CSV `raw_rpm`에는 원래 값을 남긴다. `RpmParser::kMaxRpm` = 911,420,367보다 큰 값은 버린다(속도 int 변환이 정의되는 최대값; 차량 RPM 범위에서 정한 값이 아님) | `RpmParser::correct`, `RpmParser::parseLine`, `kMaxRpm`; `MainWindow::acceptRpmSample` | `tst_rpmparser::negativeIsParsedThenClampedToZero`, `tst_rpmparser::intBoundaries`, `tst_rpmparser::robustnessInputClasses`, `tst_mainwindow::negativeSampleIsDisplayedAsZeroAndRawIsLogged`, `tst_mainwindow::speedStaysRepresentableUpToRpmBound` | mutation M11, N05, N06 killed; fuzz O6 | PASS — 상한은 **DEF-SW-01 수정**으로 추가됨 |
| **REQ-IN-006** parser 추출(리팩터링) 전후 동작이 같다. 수정(DEF-SW-01/04) 이후에는: 출력은 research-baseline 루프 출력의 부분열이고(값을 새로 만들거나 바꾸지 않음), NUL·상한 초과 줄이 없는 입력에서는 완전히 같다 | `rpmparser.h` vs `research-baseline` `readSerialData` | `tst_rpmparser::extractedParserMatchesBaselineInlineCode` | fuzz oracle O3(부분열, 무작위 입력) | PASS |
| **REQ-IN-007** parser는 임의의 바이트 입력에서 crash, 메모리 오류, 정의되지 않은 동작(UB)이 없고 O1–O5 불변식을 지킨다 | `RpmParser::consume`, `RpmParser::correct` | `tst_rpmparser::robustnessInputClasses` | `fuzz_report.md` (libFuzzer + ASan + UBSan, O1–O6), sanitizer 빌드 QtTest | PASS (fuzz 대상 범위 안에서) |
| **REQ-UI-001** UI는 33 ms 주기 timer(약 30 Hz)로, flush 사이에 들어온 샘플 중 마지막 1개만 반영한다 | `mainwindow.h` `kUiIntervalMs = 1000/30`; `MainWindow::flushPendingRpm` L100 | `tst_mainwindow::onlyLatestPendingSampleIsShownAndLogged`, `tst_mainwindow::replayPlaysAtConfiguredIntervalAndStops` | timer 주기 33 ms 자체는 정적 확인(B) | PASS (로직 A, 주기 B) |
| **REQ-UI-002** 샘플은 다음 flush에서만 표시된다. pending 샘플이 없을 때 flush해도 표시나 로그가 바뀌지 않는다 | `MainWindow::acceptRpmSample`, `MainWindow::flushPendingRpm` L102 | `tst_mainwindow::sampleIsShownOnlyAfterFlush`, `tst_mainwindow::flushWithoutPendingSampleChangesNothing` | mutation M12/M13 killed | PASS |
| **REQ-UI-003** 속도는 *표시된* RPM에서 기존 공식 `rpm × (4.5/2.5) × π × 2.5 × 10 / 60`(소수점 버림)으로 계산한다 | `MainWindow::update_values` | `tst_mainwindow::speedIsDerivedFromDisplayedRpm`, `tst_mainwindow::speedStaysRepresentableUpToRpmBound` | mutation M18 killed; strict UBSan run 0 violations | PASS — **DEF-SW-01 수정됨** (수정 전: rpm > 911,420,367이면 UB) |
| **REQ-UI-004** `CLUSTER_ANIMATE=1`일 때 변화량 < 5이면 바로 표시하고, 그 외에는 `clamp(2·delta, 60, 180)` ms 동안 현재 값에서 목표 값까지 애니메이션한다. 어떤 int 시작값/목표값에서도 이 계산에 overflow가 없어야 한다 | `MainWindow::flushPendingRpm` | `tst_mainwindow::animation`, `tst_mainwindow::animationIsRestartedFromCurrentValue`, `tst_mainwindow::animationDurationHasNoOverflow` | mutation M16/M17 killed; strict UBSan run 0 violations | PASS — **DEF-SW-02 수정됨** (수정 전: delta > INT_MAX/2이면 `delta*2` overflow) |
| **REQ-UI-005** 입력 소스를 열지 못하면 RPM 자리에 오류를 표시한다(`Replay Err`, `Open Err`) | `MainWindow::setupReplay` L250; `MainWindow::setupSerialPort` L188 | `tst_mainwindow::missingReplayFileIsReported`, `tst_mainwindow::unopenableSerialPortIsReported` | – | PASS (`Port Err`, 즉 포트 자동 탐지 실패는 host 장치 구성에 따라 달라서 미검증) |
| **REQ-MEAS-001** 하드웨어 없이 같은 입력 시퀀스로 반복 실행할 수 있다(`CLUSTER_SIMULATE`, `CLUSTER_REPLAY_FILE`) | `setupSimulation`, `generateSimulatedRpm`, `setupReplay`, `feedReplaySample` | `tst_mainwindow::replayUsesSerialLineProtocol`, `tst_mainwindow::replayPlaysAtConfiguredIntervalAndStops`, `tst_mainwindow::simulatedSamplesStayInGeneratorRange` | mutation M21 killed | PASS |
| **REQ-MEAS-002** CSV는 정해진 header를 가진다. 표시된 frame마다 paintEvent 이후에 1행을 기록하며, `e2e_latency_ms = t_frame_ms − t_in_ms`이고 raw 값과 보정 값을 함께 남긴다 | 생성자 L21–29, paint lambda L58–66, `MainWindow::logEvent` L163, `TimestampLabel::paintEvent` | `tst_mainwindow::csvHeaderIsWrittenEvenWithoutFrames`, `tst_mainwindow::eachFrameIsLoggedOnceAfterPaint`, `tst_mainwindow::paintBeforeFirstSampleIsNotLogged`, `tst_mainwindow::negativeSampleIsDisplayedAsZeroAndRawIsLogged`, `tst_mainwindow::knownDefect_repeatedValueLoggedAtUnrelatedRepaint` | mutation M15/M19/M20 killed | PASS + **Known Measurement Limitation DEF-SW-03** (수정하지 않음, 아래 참조) |
| **REQ-MEAS-003** CSV 파일을 만들 수 없어도 표시 경로는 계속 동작하고, 로깅만 생략한다 | 생성자 L27, `MainWindow::logEvent` L165 | `tst_mainwindow::displayWorksWhenCsvCannotBeCreated` | coverage: 두 분기 모두 실행됨 | PASS |
| **REQ-PERF-001** (분석 로직) 지연 CSV를 `p95 ≤ 33.3 ms` soft budget으로 판정한다. budget과 같으면 PASS, 초과하면 FAIL이다. exit code는 PASS 0, FAIL 1, 입력 오류 2다 | `scripts/validate_latency.py` `summarize`, `main`; `scripts/analyze_latency.py` `percentile` | `test_latency_analysis::test_percentile_matches_linear_interpolation`, `test_latency_analysis::test_summary_statistics_and_budget`, `test_latency_analysis::test_verdict_fails_when_p95_exceeds_budget`, `test_latency_analysis::test_verdict_boundary_p95_equal_to_budget_passes`, `test_latency_analysis::test_cli_exit_codes`, `test_latency_analysis::test_missing_columns_is_input_error` | mutation P01/P03 killed | PASS (판정 로직만 해당. 하드웨어 성능은 HW-001) |
| **REQ-PERF-002** 지연 보고서는 로그되지 않은 샘플 수(seq 공백)를 보고한다 | `validate_latency.coalesced_samples` | `test_latency_analysis::test_seq_gaps_count_samples_not_logged` | mutation P02 killed | PASS |
| **REQ-HW-001** 실제 RUBIK Pi 3에서 측정 구간(`sample accepted → QLabel paintEvent completed`)의 지연이 30 Hz 기준(약 33.3 ms) 안에 든다 | 측정 코드는 research-baseline과 동일 | – | **Historical research result**: 1,000회, 평균 10.38 ms, 최대 21 ms (원본 CSV는 저장소에 없음) | **Hardware-dependent / Not revalidated** |
| **REQ-HW-002** Arduino에서 UART 9600 baud로 보낸 RPM 줄을 앱이 받는다 | `setupSerialPort`, `readSerialData` | (pty 테스트는 앱 내부 경로만 확인하며 UART가 아니다) | – | **Hardware-dependent / Not revalidated** |

## 집계

| 구분 | 수정 전 (`f9e211c`) | 수정 후 |
|---|---:|---:|
| 검증 가능한 SW requirement (IN 7 + UI 5 + MEAS 3 + PERF 2) | 17 | 17 |
| 그중 1개 이상의 test와 연결된 것 | 17 / 17 | **17 / 17** |
| ├ PASS | 13 | **16** |
| └ PASS + 결함/한계 기록 | 4 (DEF-SW-01~04) | **1** (DEF-SW-03, Known Measurement Limitation) |
| Hardware-dependent / Not revalidated | 2 | 2 (REQ-HW-001, REQ-HW-002) — 개수에 포함하지 않음 |
| 문서/절차 요구사항(REQ-PERF-003) | 테스트 대상 아님 | 테스트 대상 아님 |

## 이번 검증에서 발견한 결함 4건: 수정 3 / 미수정 1

| ID | 내용 | 상태 | 수정 방법 | 확인 |
|---|---|---|---|---|
| DEF-SW-01 | `update_values()`: `int(rpm × 2.356…)`이 rpm > 911,420,367에서 int로 표현되지 않음(UB, UBSan float-cast-overflow; x86-64에서 속도 −2147483648). parser에 상한이 없어 시리얼 `2147483647\n` 한 줄로 도달 | **수정** (`3b734b9`) | `RpmParser::kMaxRpm = 911420367` — 그 식과 int 타입에서 유도한 값(911,420,367 → 2147483646.97, 911,420,368 → 2147483649.33). 넘는 값은 int 범위 밖 값처럼 그 줄을 버린다. 음수의 0 보정은 그대로 | `speedStaysRepresentableUpToRpmBound` PASS(이제 strict UBSan 빌드에서도 실행), bound 행 PASS, UBSan 위반 0 |
| DEF-SW-02 | `flushPendingRpm()`: `delta * 2`가 delta > 1,073,741,823에서 int overflow(UB); x86-64에서 애니메이션 시간이 180이 아니라 60 ms | **수정** (`49a6d6c`) | `delta`를 64비트로 계산하고 `2 * clamp(delta, 30, 90)` 사용. 이는 overflow가 없는 모든 delta에서 `clamp(2·delta, 60, 180)`과 같은 값이다(정상 동작 불변). DEF-SW-01 이후 입력 경로로는 delta ≤ 911,420,367이라 도달할 수 없지만, 계산 자체를 안전하게 만들었다 | `animationDurationHasNoOverflow` PASS(공개 `displayRpm` 속성으로 시작값 설정), `animation` 6행 PASS, UBSan 위반 0 |
| DEF-SW-04 | Qt 5.12.8 `QString::toInt()`가 NUL에서 멈춰 `"7\0abc"`→7, `"25\0" "00"`→25를 수용 | **수정** (`0262f29`) | `RpmParser::parseLine()`: trim 후 줄 **전체**가 `[+\|-]숫자+`인지 바이트 단위로 확인한 뒤 변환한다. NUL만 막는 특수 처리가 아니라 숫자가 아닌 모든 바이트를 같은 규칙으로 거부한다 | 형식 계약 표 37행 + 읽기 경계 NUL 테스트 PASS(공백·부호·꼬리 garbage·빈 입력·overflow·프레임 경계 포함), fuzz O6(독립 참조 구현) 위반 0 |
| DEF-SW-03 | 표시 값과 같은 샘플은 setText와 paint를 일으키지 않지만 "현재 frame"으로 등록된다. 그래서 그 뒤 다른 이유로 rpmLabel이 repaint되면 그 시점이 `t_frame`으로 기록된다 | **미수정 — Known Measurement Limitation** | 수정하지 않음. 이유: 연구 당시 측정 의미와 직접 관련되어 있고, 원본 CSV가 없어 수정 전후를 비교할 수 없다 | `knownDefect_repeatedValueLoggedAtUnrelatedRepaint` XFAIL 유지 |

### DEF-SW-03에 대해 말할 수 있는 것과 없는 것

- **사실:** 이 동작은 host 환경(Qt 5.12.8 offscreen)에서 **실제로 재현됐다.** 기존 `measurement_boundary.md`에는 정적 분석(B) 수준의 "가능성"으로 적혀 있었다.
- **판단할 수 없음:** 연구 당시 결과(1,000회, 평균 10.38 ms, 최대 21 ms)에 이 동작이 얼마나 영향을 주었는지는 **판단할 수 없다.** 원본 측정 CSV가 저장소에 없고, 연구 당시 입력 패턴(같은 값이 연속으로 들어온 빈도)과 rpmLabel이 다른 이유로 repaint된 빈도를 알 수 없다.
- 따라서 Historical Result가 **무효라고 단정하지 않는다.** 동시에 **영향이 없었다고도 단정하지 않는다.**
