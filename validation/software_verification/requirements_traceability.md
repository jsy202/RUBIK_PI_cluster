# Requirements Traceability — Post-project Software Verification

> 기존 실제 하드웨어 연구 이후, host 기반 환경(개발 PC + Docker, Ubuntu 20.04, Qt 5.12.8, offscreen)에서
> SW 입력 처리와 회귀시험의 검증 범위를 coverage, mutation, fuzz testing으로 확장한 결과다.
> RUBIK Pi 3에서 다시 실행한 결과가 아니다.

ID 체계는 기존 [`validation/requirements.md`](../requirements.md)를 그대로 잇는다. 기존 ID(IN-001~006, UI-001, PERF-001/002, MEAS-001)는 의미를 바꾸지 않았다. 이번에 추가한 ID는 IN-007, UI-002~005, MEAS-002/003, HW-001/002다.
모든 요구사항은 **현재 코드가 실제로 하는 동작**에서 도출했다. 이상적인 사양을 새로 만든 것이 아니다.

Result 열의 의미:
- **PASS**: 연결된 테스트가 모두 통과한다.
- **PASS + known deviation**: 정상 범위는 통과하지만, 요구사항에 어긋나는 입력이 재현됐다. `QEXPECT_FAIL`로 기록했고 수정하지 않았다(아래 defect 목록 참조).
- **Hardware-dependent / Not revalidated**: 하드웨어가 있어야 검증할 수 있다. 이번 작업에서 다시 검증하지 않았다.

테스트 이름 표기: `tst_rpmparser::…`, `tst_mainwindow::…`는 QtTest 함수, `test_latency_analysis::…`는 pytest 함수다. 이 표에 적힌 모든 테스트 이름이 실제 테스트 코드에 존재하는지는 `tests/analysis/test_traceability.py`가 확인한다(CI에서 실행).

## Traceability Matrix

| Requirement | Production Code | Test Case | Evidence | Result |
|---|---|---|---|---|
| **REQ-IN-001** 시리얼 입력은 `\n`으로 끝나는 줄마다 10진 정수 1개다. 앞뒤 공백, `\r`, `+` 부호, 앞자리 0은 허용한다 | `rpmparser.h` `RpmParser::consume` L19; `mainwindow.cpp` `readSerialData` L91 | `tst_rpmparser::normalValue`, `tst_rpmparser::zero`, `tst_rpmparser::intBoundaries`, `tst_rpmparser::crlfAndSurroundingSpacesAreTolerated`, `tst_rpmparser::multipleFramesInOneRead`, `tst_rpmparser::repeatedValuesAreAllReturned`, `tst_rpmparser::rapidChangesKeepOrder`, `tst_rpmparser::robustnessInputClasses`, `tst_mainwindow::serialPathViaPseudoTerminal`, `tst_mainwindow::replayUsesSerialLineProtocol` | QtTest log, mutation M06/M07 killed | PASS |
| **REQ-IN-002** 정수가 아니거나, int 범위를 벗어나거나, 빈 줄이면 그 줄만 버리고 다음 줄을 계속 처리한다 | `RpmParser::consume` (`toInt(&ok)` → `if (!ok) continue`) L36–37 | `tst_rpmparser::overflowIsSkipped`, `tst_rpmparser::invalidStringsAreSkipped`, `tst_rpmparser::emptyAndWhitespaceLinesAreSkipped`, `tst_rpmparser::robustnessInputClasses`, `tst_mainwindow::serialPathViaPseudoTerminal` | QtTest log (XFAIL 2행), mutation M06/M07 killed | PASS + known deviation **DEF-SW-04** (NUL 바이트 뒤 내용을 무시하고 앞부분 정수를 수용) |
| **REQ-IN-003** 한 줄이 여러 번의 read에 나뉘어 와도 하나의 값으로 조립하고, 끝나지 않은 꼬리는 다음 read까지 버퍼에 남긴다 | `RpmParser::consume` (persistent buffer) | `tst_rpmparser::splitFrameAcrossReads`, `tst_rpmparser::unterminatedTailIsKeptForNextRead`, `tst_mainwindow::serialPathViaPseudoTerminal` | QtTest log; fuzz oracle O4(분할 불변성) | PASS |
| **REQ-IN-004** 64바이트를 넘는 줄은 버린다. 버퍼가 8 KiB를 넘으면 가장 오래된 바이트부터 버린다 | `RpmParser::consume` L25–26, L33; `mainwindow.h` `kMaxLineLen`, `kMaxBufferBytes` | `tst_rpmparser::lineAtMaxLengthIsAcceptedLongerIsSkipped`, `tst_rpmparser::bufferOverflowDropsOldestBytes`, `tst_rpmparser::robustnessInputClasses` | mutation M02–M05 killed; fuzz oracle O1; negative control `cap_removed` | PASS |
| **REQ-IN-005** 음수 RPM은 표시할 때 0으로 보정하고, CSV `raw_rpm`에는 원래 값을 남긴다. 상한은 없다 | `RpmParser::correct` L45; `MainWindow::acceptRpmSample` L174 | `tst_rpmparser::negativeIsParsedThenClampedToZero`, `tst_mainwindow::negativeSampleIsDisplayedAsZeroAndRawIsLogged` | mutation M11 killed | PASS (상한이 없어서 DEF-SW-01/02가 생긴다) |
| **REQ-IN-006** parser를 추출하기 전(research-baseline 루프)과 후의 출력, 버퍼가 같다 | `rpmparser.h` vs `research-baseline` `readSerialData` | `tst_rpmparser::extractedParserMatchesBaselineInlineCode` | fuzz oracle O3(차등, 무작위 입력) | PASS |
| **REQ-IN-007** parser는 임의의 바이트 입력에서 crash, 메모리 오류, 정의되지 않은 동작(UB)이 없고 O1–O5 불변식을 지킨다 | `RpmParser::consume`, `RpmParser::correct` | `tst_rpmparser::robustnessInputClasses` | `fuzz_report.md` (libFuzzer + ASan + UBSan), sanitizer 빌드 QtTest | PASS (fuzz 대상 범위 안에서) |
| **REQ-UI-001** UI는 33 ms 주기 timer(약 30 Hz)로, flush 사이에 들어온 샘플 중 마지막 1개만 반영한다 | `mainwindow.h` `kUiIntervalMs = 1000/30`; `MainWindow::flushPendingRpm` L100 | `tst_mainwindow::onlyLatestPendingSampleIsShownAndLogged`, `tst_mainwindow::replayPlaysAtConfiguredIntervalAndStops` | timer 주기 33 ms 자체는 정적 확인(B) | PASS (로직 A, 주기 B) |
| **REQ-UI-002** 샘플은 다음 flush에서만 표시된다. pending 샘플이 없을 때 flush해도 표시나 로그가 바뀌지 않는다 | `MainWindow::acceptRpmSample`, `MainWindow::flushPendingRpm` L102 | `tst_mainwindow::sampleIsShownOnlyAfterFlush`, `tst_mainwindow::flushWithoutPendingSampleChangesNothing` | mutation M12/M13 killed | PASS |
| **REQ-UI-003** 속도는 *표시된* RPM에서 기존 공식 `rpm × (4.5/2.5) × π × 2.5 × 10 / 60`(소수점 버림)으로 계산한다 | `MainWindow::update_values` L144 | `tst_mainwindow::speedIsDerivedFromDisplayedRpm`, `tst_mainwindow::speedStaysRepresentableUpToRpmBound` | mutation M18 killed; UBSan report | PASS + known deviation **DEF-SW-01** (rpm > 약 9.1×10⁸이면 int 변환에서 UB) |
| **REQ-UI-004** `CLUSTER_ANIMATE=1`일 때 변화량 < 5이면 바로 표시하고, 그 외에는 `clamp(2·delta, 60, 180)` ms 동안 현재 값에서 목표 값까지 애니메이션한다 | `MainWindow::flushPendingRpm` L124–141 | `tst_mainwindow::animation`, `tst_mainwindow::animationIsRestartedFromCurrentValue`, `tst_mainwindow::knownDefect_animationDurationOverflow` | mutation M16/M17 killed; UBSan report | PASS + known deviation **DEF-SW-02** (delta > INT_MAX/2이면 `delta*2` overflow) |
| **REQ-UI-005** 입력 소스를 열지 못하면 RPM 자리에 오류를 표시한다(`Replay Err`, `Open Err`) | `MainWindow::setupReplay` L250; `MainWindow::setupSerialPort` L188 | `tst_mainwindow::missingReplayFileIsReported`, `tst_mainwindow::unopenableSerialPortIsReported` | – | PASS (`Port Err`, 즉 포트 자동 탐지 실패는 host 장치 구성에 따라 달라서 미검증) |
| **REQ-MEAS-001** 하드웨어 없이 같은 입력 시퀀스로 반복 실행할 수 있다(`CLUSTER_SIMULATE`, `CLUSTER_REPLAY_FILE`) | `setupSimulation`, `generateSimulatedRpm`, `setupReplay`, `feedReplaySample` | `tst_mainwindow::replayUsesSerialLineProtocol`, `tst_mainwindow::replayPlaysAtConfiguredIntervalAndStops`, `tst_mainwindow::simulatedSamplesStayInGeneratorRange` | mutation M21 killed | PASS |
| **REQ-MEAS-002** CSV는 정해진 header를 가진다. 표시된 frame마다 paintEvent 이후에 1행을 기록하며, `e2e_latency_ms = t_frame_ms − t_in_ms`이고 raw 값과 보정 값을 함께 남긴다 | 생성자 L21–29, paint lambda L58–66, `MainWindow::logEvent` L163, `TimestampLabel::paintEvent` | `tst_mainwindow::csvHeaderIsWrittenEvenWithoutFrames`, `tst_mainwindow::eachFrameIsLoggedOnceAfterPaint`, `tst_mainwindow::paintBeforeFirstSampleIsNotLogged`, `tst_mainwindow::negativeSampleIsDisplayedAsZeroAndRawIsLogged`, `tst_mainwindow::knownDefect_repeatedValueLoggedAtUnrelatedRepaint` | mutation M15/M19/M20 killed | PASS + known deviation **DEF-SW-03** (같은 값 샘플이 관계없는 repaint 시점에 기록됨) |
| **REQ-MEAS-003** CSV 파일을 만들 수 없어도 표시 경로는 계속 동작하고, 로깅만 생략한다 | 생성자 L27, `MainWindow::logEvent` L165 | `tst_mainwindow::displayWorksWhenCsvCannotBeCreated` | coverage: 두 분기 모두 실행됨 | PASS |
| **REQ-PERF-001** (분석 로직) 지연 CSV를 `p95 ≤ 33.3 ms` soft budget으로 판정한다. budget과 같으면 PASS, 초과하면 FAIL이다. exit code는 PASS 0, FAIL 1, 입력 오류 2다 | `scripts/validate_latency.py` `summarize`, `main`; `scripts/analyze_latency.py` `percentile` | `test_latency_analysis::test_percentile_matches_linear_interpolation`, `test_latency_analysis::test_summary_statistics_and_budget`, `test_latency_analysis::test_verdict_fails_when_p95_exceeds_budget`, `test_latency_analysis::test_verdict_boundary_p95_equal_to_budget_passes`, `test_latency_analysis::test_cli_exit_codes`, `test_latency_analysis::test_missing_columns_is_input_error` | mutation P01/P03 killed | PASS (판정 로직만 해당. 하드웨어 성능은 HW-001) |
| **REQ-PERF-002** 지연 보고서는 로그되지 않은 샘플 수(seq 공백)를 보고한다 | `validate_latency.coalesced_samples` | `test_latency_analysis::test_seq_gaps_count_samples_not_logged` | mutation P02 killed | PASS |
| **REQ-HW-001** 실제 RUBIK Pi 3에서 측정 구간(`sample accepted → QLabel paintEvent completed`)의 지연이 30 Hz 기준(약 33.3 ms) 안에 든다 | 측정 코드는 research-baseline과 동일 | – | **Historical research result**: 1,000회, 평균 10.38 ms, 최대 21 ms (원본 CSV는 저장소에 없음) | **Hardware-dependent / Not revalidated** |
| **REQ-HW-002** Arduino에서 UART 9600 baud로 보낸 RPM 줄을 앱이 받는다 | `setupSerialPort`, `readSerialData` | (pty 테스트는 앱 내부 경로만 확인하며 UART가 아니다) | – | **Hardware-dependent / Not revalidated** |

## 집계

| 구분 | 개수 |
|---|---:|
| 검증 가능한 SW requirement (IN 7 + UI 5 + MEAS 3 + PERF 2) | **17** |
| 그중 1개 이상의 test와 연결된 것 | **17 / 17** |
| ├ PASS | 13 |
| └ PASS + known deviation (DEF-SW-01~04, 수정 안 함) | 4 |
| Hardware-dependent / Not revalidated | 2 (REQ-HW-001, REQ-HW-002) — 개수에 포함하지 않음 |
| 문서/절차 요구사항(REQ-PERF-003, 측정 환경 기록) | 테스트 대상 아님, 개수에 포함하지 않음 |

## 이번 검증에서 새로 발견한 결함 (수정하지 않음)

| ID | 내용 | 재현 | 영향 범위 |
|---|---|---|---|
| DEF-SW-01 | `update_values()`: `int(rpm × 2.356…)`에서 rpm이 약 9.1×10⁸보다 크면 int로 표현할 수 없다. 정의되지 않은 동작(UBSan float-cast-overflow)이고, x86-64에서는 속도가 −2147483648로 표시된다 | `tst_mainwindow::speedStaysRepresentableUpToRpmBound`, `sanitize_known_defects.txt` | parser에 상한이 없어서 시리얼에서 `2147483647\n` 한 줄만 와도 도달한다. 정상 RPM 범위와는 관계없다 |
| DEF-SW-02 | `flushPendingRpm()`: `delta * 2`가 delta > 1,073,741,823일 때 int overflow를 일으킨다(UBSan signed-integer-overflow). 애니메이션 시간이 180 ms가 아니라 60 ms로 잘린다 | `tst_mainwindow::knownDefect_animationDurationOverflow` | `CLUSTER_ANIMATE=1`일 때만 해당한다(기본 비활성, 측정 시 끔) |
| DEF-SW-03 | 표시 값과 같은 샘플은 setText와 paint를 일으키지 않지만 "현재 frame"으로 등록된다. 그래서 다른 이유로 일어난 다음 repaint 시점이 `t_frame`으로 기록된다 | `tst_mainwindow::knownDefect_repeatedValueLoggedAtUnrelatedRepaint` | 측정 의미에 관한 결함이다. 기존 `measurement_boundary.md`에 "가능성(B)"으로 적혀 있던 것을 실행(A)으로 재현했다. 연구 당시 수치에 영향이 있었는지는 원본 CSV가 없어서 **판단할 수 없다** |
| DEF-SW-04 | Qt 5.12.8의 `QString::toInt()`는 문자열 안의 NUL에서 파싱을 멈춘다. 그래서 `"7\0abc"`는 7로, `"25\0" "00"`은 25로 수용되고, `"12a"`처럼 거부되지 않는다 | `tst_rpmparser::robustnessInputClasses` (XFAIL 2행) | 시리얼 잡음으로 NUL이 들어오면 잘못된 값을 받아들인다. Qt 버전마다 다를 수 있다(5.12.8에서만 확인) |

수정하지 않은 이유: production 동작 변경은 이번 범위가 아니다. 수정하면 연구 코드가 바뀌므로 별도로 결정해야 한다. 각 결함은 `QEXPECT_FAIL`로 고정해 두었다. 동작이 바뀌면 XPASS가 되고 테스트가 실패하므로, 그때 이 기록을 갱신하면 된다.
