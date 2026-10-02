# Mutation Testing & Negative Control — Post-project Software Verification

> 환경: 개발 PC + Docker(`Dockerfile.verify`, Ubuntu 20.04, Qt 5.12.8, GCC 9.3). RUBIK Pi 3 결과가 아니다.
> 실행: `scripts/sw_verify.sh mutate` → `scripts/mutation_test.py`. 원본: [`evidence/mutation_results.json`](evidence/mutation_results.json), [`evidence/console_mutate.txt`](evidence/console_mutate.txt).

## 결함 수정 후 결과 (`dae0620`) — 최신

수정 코드(DEF-SW-01/02/04)에 맞춰 mutant 목록을 갱신했다. 기존 24개는 같은 결함 유형을 유지하면서 새 코드의 해당 위치를 가리키도록 패턴만 바꿨다(M06, M07, M09, M17). 수정 코드 자체를 겨냥한 N01–N06 6개를 추가했다. 원본: [`evidence/after_fixes/mutation_results.json`](evidence/after_fixes/mutation_results.json).

| 항목 | 수정 전 (`f9e211c`) | 수정 후 (`dae0620`) |
|---|---:|---:|
| 총 mutation | 24 | 30 |
| killed | 21 | 27 |
| survived | 0 | 0 |
| equivalent (점수 제외) | 3 (M01, M09, M10) | 3 (M01, M10, N04) |
| invalid | 0 | 0 |
| **Mutation Score** | 21/21 = 100% | **27/27 = 100%** |

| ID | 변경 | 모사하는 결함 | 결과 | 검출한 테스트 (대표) |
|---|---|---|---|---|
| N01 | 숫자 형식 검사 → `if (false)` | DEF-SW-04 재발 | killed | `nulAcrossReadBoundaryIsRejected`, `robustnessInputClasses(NUL …)` |
| N02 | `t.at(i) > '9'` → `>= '9'` | 숫자 범위 경계 | killed | `animation(delta 90 …)`, `speedStaysRepresentableUpToRpmBound` 외 |
| N03 | `+` 부호 허용 제거 | 기존 허용 형식 회귀 | killed | `robustnessInputClasses(explicit plus sign)`, `extractedParserMatchesBaselineInlineCode` |
| N04 | 빈 줄·부호만 있는 줄 guard 삭제 | – | **equivalent** | – (아래 근거) |
| N05 | `value <= kMaxRpm` → `<` | 상한 경계 off-by-one | killed | `robustnessInputClasses(bound: largest …)`, `intBoundaries` 외 |
| N06 | 상한 검사 삭제 | DEF-SW-01 재발 | killed | `speedStaysRepresentableUpToRpmBound`, `robustnessInputClasses(bound: INT_MAX)` 외 |
| M09 | `line.trimmed()` → `line` | 공백 제거 누락 | killed (수정 전에는 equivalent) | `crlfAndSurroundingSpacesAreTolerated`, `serialPathViaPseudoTerminal` 외 |
| M17 | `2 * clamp(delta, 30, 90)` → `clamp(delta, 30, 90)` | 애니메이션 시간 변환 오류 | killed | `animation(delta 45/90/1000 …)` |

**Equivalent 근거:**
- N04: guard가 없어도 빈 문자열과 `"+"`, `"-"`는 숫자 검사를 통과한 뒤 `QByteArray::toInt()`에서 `ok == false`가 되어 거부된다. guard는 이 경우를 일찍 끝내는 방어 코드다.
- M09이 이제 equivalent가 아닌 이유: 새 형식 검사는 숫자가 아닌 바이트(공백 포함)를 거부하므로 trim이 꼭 필요하다.

"100%"는 여전히 **직접 고른 mutant 30개** 기준이다. 독립적인 품질 지표가 아니다.

## 수정 전 실행 (`f9e211c`) 방법

- 자동 mutation framework를 쓰지 않았다. 실제 코드의 결정문과 계산식에서 **손으로 고른 24개**의 결함을 대상으로 했다(C++ 21, Python 3).
- 각 mutant는 production 파일 하나에 텍스트 치환 1개를 적용한 것이다. 저장소를 **임시 디렉터리에 복사한 뒤 그 사본에만** 적용하고, mutant마다 원래 파일로 되돌린다. 원본(`/src`)은 Docker에 **읽기 전용**으로 마운트했다. 실행이 끝나면 사본의 production 파일이 원본과 바이트 단위로 같은지 검사한다. mutant 상태는 커밋된 적이 없다.
- 판정은 다음과 같다.
  - **killed**: QtTest parser, QtTest MainWindow, pytest 중 하나라도 실패한다(XPASS 포함). 또는 60초 timeout에 걸린다.
  - **survived**: 모든 테스트가 통과한다.
  - **invalid**: 컴파일되지 않는다.
- **equivalent**: 코드 검토로 동작이 바뀌지 않는다고 판단한 mutant다. 미리 표시해 두고, 실행은 하되 점수에서 뺀다. 억지로 죽이는 테스트를 만들지 않았다.

## 수정 전 결과 (`f9e211c`)

| 항목 | 값 |
|---|---:|
| 총 mutation | 24 |
| killed | 21 |
| survived | 0 |
| equivalent (점수 제외) | 3 |
| invalid | 0 |
| **Mutation Score** (killed / (총 − equivalent − invalid)) | **21/21 = 100%** |

**해석할 때 주의할 점:** 100%는 *내가 고른 24개* 결함에 대한 값이다. 또한 테스트 1건(P01용)은 첫 실행에서 살아남은 mutant를 보고 **나중에 추가했다.** 그 테스트를 추가하기 전의 결과는 **19/20 = 95%**였다(P01 survived, 그리고 harness 패턴 오류로 M14 invalid. 근거: [`evidence/mutation_results_before_P01_test.json`](evidence/mutation_results_before_P01_test.json)). 따라서 이 점수는 독립적인 품질 지표가 아니라, "이 결함 유형들은 현재 테스트가 잡는다"는 확인으로 읽어야 한다.

## Mutant 목록

| ID | 위치 | 변경 | 모사하는 결함 | 결과 | 검출한 테스트 (대표) |
|---|---|---|---|---|---|
| M01 | `rpmparser.h` L25 | `size() > max` → `>=` | 버퍼 상한 비교 경계 | **equivalent** | – |
| M02 | `rpmparser.h` L26 | 오래된 바이트 제거 → `;` | 8 KiB 상한 제거(무한 버퍼) | killed | `extractedParserMatchesBaselineInlineCode` |
| M03 | `rpmparser.h` L26 | `remove(0, n)` → `truncate(max)` | 넘칠 때 새 바이트를 버림(정책 반대) | killed | `bufferOverflowDropsOldestBytes` 외 2 |
| M04 | `rpmparser.h` L33 | `> maxLineLen` → `>=` | 줄 길이 경계 off-by-one(64바이트 줄 거부) | killed | `lineAtMaxLengthIsAcceptedLongerIsSkipped` |
| M05 | `rpmparser.h` L33 | 줄 길이 검사 삭제 | 긴 줄 거부 누락 | killed | `lineAtMaxLengthIsAcceptedLongerIsSkipped` |
| M06 | `rpmparser.h` L37 | `if (!ok)` → `if (ok)` | parse 성공/실패 조건 반전 | killed | parser·MainWindow 다수 (46) |
| M07 | `rpmparser.h` L37 | `if (!ok) continue;` 삭제 | 잘못된 입력 거부 누락(0으로 수용) | killed | `invalidStringsAreSkipped` 외 11 |
| M08 | `rpmparser.h` L31 | `remove(0, nl + 1)` → `nl` | 줄바꿈을 소비하지 않음(무한 루프) | killed (timeout) | 두 QtTest 모두 timeout |
| M09 | `rpmparser.h` L36 | `.trimmed()` 삭제 | 공백 제거 누락 | **equivalent** | – |
| M10 | `rpmparser.h` L47 | `rawRpm < 0` → `<= 0` | 음수 보정 경계 | **equivalent** | – |
| M11 | `rpmparser.h` L47 | 보정 제거(`return rawRpm`) | 음수 RPM 표시 | killed | `negativeIsParsedThenClampedToZero`, `negativeSampleIsDisplayedAsZeroAndRawIsLogged` 외 2 |
| M12 | `mainwindow.cpp` L102 | `if (m_pendingRpm < 0) return;` 삭제 | pending이 없을 때 flush를 무시하지 않음 | killed | `flushWithoutPendingSampleChangesNothing`, `animation` 외 |
| M13 | `mainwindow.cpp` L185 | `m_pendingRpm = finalRpm;` 삭제 | 받은 값을 저장하지 않음(갱신 생략) | killed | `sampleIsShownOnlyAfterFlush` 외 다수 |
| M14 | `mainwindow.cpp` L84 | `m_displayRpm = rpm;` → `;` | 표시 상태 갱신 생략 | killed | `speedIsDerivedFromDisplayedRpm`, `animation` 외 |
| M15 | `mainwindow.cpp` L59 | `… \|\| m_frameSeq == m_lastLoggedSeq` 삭제 | frame당 1회 로그 보장 제거(중복 기록) | killed | `eachFrameIsLoggedOnceAfterPaint` 외 2 |
| M16 | `mainwindow.cpp` L130 | `delta < kSmallDelta` → `<=` | 애니메이션 임계값 경계 | killed | `animation(delta 5 …)` |
| M17 | `mainwindow.cpp` L135 | `clamp(delta * 2, …)` → `clamp(delta, …)` | 애니메이션 시간 변환 오류 | killed | `animation(delta 45/90 …)` |
| M18 | `mainwindow.cpp` L148 | `(4.5 / 2.5)` → `(2.5 / 4.5)` | 속도 변환비 반전 | killed | `speedIsDerivedFromDisplayedRpm` |
| M19 | `mainwindow.cpp` L167 | `t_paint - t_serial` → 반대 | CSV latency 부호 반전 | killed | `eachFrameIsLoggedOnceAfterPaint` |
| M20 | `mainwindow.cpp` L178 | `m_seq++;` 삭제 | seq 번호가 증가하지 않음(로그 안 됨) | killed | `eachFrameIsLoggedOnceAfterPaint` 외 8 |
| M21 | `mainwindow.cpp` L276 | `>= size()` → `>` | replay 끝 경계(범위 밖 읽기) | killed | `replayUsesSerialLineProtocol`, `replayPlaysAtConfiguredIntervalAndStops` |
| P01 | `validate_latency.py` L63 | `<= budget` → `<` | PASS/FAIL 판정 경계 | killed | `test_verdict_boundary_p95_equal_to_budget_passes` |
| P02 | `validate_latency.py` L36 | `b - a - 1` → `b - a` | 로그되지 않은 샘플 수 off-by-one | killed | `test_seq_gaps_count_samples_not_logged` |
| P03 | `analyze_latency.py` L33 | percentile rank 공식 변경 | 통계 계산 오류 | killed | `test_percentile_matches_linear_interpolation` 외 6 |

### Equivalent 판정 근거

| ID | 근거 |
|---|---|
| M01 | 크기가 정확히 상한과 같을 때 `remove(0, 0)`이 실행되는데, 이는 아무것도 바꾸지 않는다 |
| M09 | Qt 5.12의 `QString::toInt()`는 앞뒤 공백을 스스로 무시한다. `QByteArray::trimmed()`가 지우는 ASCII 공백(`\t\n\v\f\r `)은 그 부분집합이다. Qt 5.12.8에서만 확인했다. 이 결론은 기존 negative control(`validation/evidence/negative_control.txt`)과도 같다 |
| M10 | 입력이 0이면 두 식 모두 0을 반환한다 |

## 첫 실행에서 드러난 것

1. **테스트 공백 1건 (P01 survived):** `p95 == budget`이면 PASS여야 한다(REQ-PERF-001은 `≤`). 그런데 이 경계를 확인하는 테스트가 없었다. 당시 line/branch coverage는 이미 100%였다. 그래서 `test_verdict_boundary_p95_equal_to_budget_passes`를 추가했다.
2. **harness 결함(수정함):**
   - Python mutant 다음에 C++ 바이너리를 다시 빌드하지 않아, 직전 C++ mutant가 남은 바이너리로 테스트가 돌았다. 이제는 C++ mutant를 되돌릴 때마다 다시 빌드한다.
   - `mainwindow.cpp`에 CRLF와 LF 줄바꿈이 섞여 있어 텍스트 모드로 쓰면 원본이 바뀐다. 이제 `newline=""`로 읽고 쓴다.
   - 줄 끝 패턴 때문에 M14가 매칭되지 않았다(invalid).

   종료 시 실행하는 사본-원본 바이트 비교가 2번째 문제를 잡아냈다.

## Negative Control (Phase 6)

대표적인 production 결함을 **임시 사본에** 의도적으로 넣고, 관련 검증 수단이 실제로 실패하는지 확인했다. production 브랜치에는 아무것도 남기지 않았다. 실행: `scripts/sw_verify.sh negctl`.

| # | 넣은 결함 | 기대 | 실제 결과 | Evidence |
|---|---|---|---|---|
| NC-1 | parser 검증 제거 (`if (!ok) continue;` 삭제 = M07) | parser 테스트 실패 | **실패함**: `invalidStringsAreSkipped`, `emptyAndWhitespaceLinesAreSkipped`, `overflowIsSkipped` 외 (QtTest 2종) | `evidence/negctl_tests.json` |
| NC-2 | 경계 조건 변경 (`> maxLineLen` → `>=` = M04) | 해당 경계 테스트 실패 | **실패함**: `lineAtMaxLengthIsAcceptedLongerIsSkipped` 1건만 | 같은 파일 |
| NC-3 | 측정 계산 반전 (`t_paint - t_serial` → 반대 = M19) | CSV 테스트 실패 | **실패함**: `eachFrameIsLoggedOnceAfterPaint` | 같은 파일 |
| NC-4 | 8 KiB 버퍼 상한 제거 → **fuzzer** | fuzz oracle 위반 | **검출, 1초 이내**. 9 KiB seed 포함 시 O1, seed 없이 O3로 검출. 단, 첫 설정에서는 120초 동안 **검출 실패**했다(아래 참조) | `evidence/negctl_fuzz_cap_removed*.txt` |
| NC-5 | parser에 범위 밖 읽기 삽입(`buffer.constData() + nl - 70`) → **ASan** | sanitizer 보고 | **검출, 0초**: `AddressSanitizer: heap-use-after-free` | `evidence/negctl_fuzz_oob_read.txt` |
| NC-6 | (참고) 알려진 UB DEF-SW-01/02 → **UBSan** | sanitizer 보고 | **검출**: `mainwindow.cpp:150 float-cast-overflow`, `:135 signed-integer-overflow` | `evidence/sanitize_known_defects.txt` |

**NC-4가 실제로 바꾼 것:** 처음 설정(libFuzzer 기본 `len_control`)에서는 입력 길이 상한이 120초 동안 2,226바이트까지밖에 오르지 않았다. 그래서 8 KiB를 넘는 입력이 생성되지 않았고, 상한 제거 결함을 **검출하지 못했다**(421,471회 실행, exit 0. `evidence/negctl_fuzz_cap_removed_run1_before_len_control.txt`). 첫 10분 fuzz 실행도 같은 설정이었으므로 버퍼 상한 경로를 사실상 시험하지 못했다고 봐야 한다. 그래서 `-len_control=0`(처음부터 최대 12,000바이트)과 9 KiB seed를 추가했다. 최종 fuzz 결과는 이 설정으로 다시 실행한 값이다.

### 수정 후 negative control (`dae0620`)

| # | 넣은 결함 / 대상 | 기대 | 실제 결과 | Evidence |
|---|---|---|---|---|
| NC-1~3 | 위와 같음 (M07, M04, M19) | 테스트 실패 | **3/3 실패함** | `evidence/after_fixes/negctl_tests.json` |
| NC-4 | 8 KiB 상한 제거 (seed 포함 / seed 없음) | fuzz oracle | **둘 다 1초 이내 O1** | `evidence/after_fixes/negctl_fuzz_cap_removed*.txt` |
| NC-5 | 범위 밖 읽기 | ASan | **0초 검출** (heap-use-after-free) | `evidence/after_fixes/negctl_fuzz_oob_read.txt` |
| NC-7 | RPM 상한 검사 제거 (DEF-SW-01 재발) | fuzz oracle O6 | **1초 이내 O6 위반** | `evidence/after_fixes/negctl_fuzz_bound_removed.txt` |
| NC-8 | 숫자 형식 검사 제거 (DEF-SW-04 재발) | fuzz oracle O6 | **1초 이내 O6 위반** | `evidence/after_fixes/negctl_fuzz_format_check_removed.txt` |
| NC-9 | 수정 전 production 코드 + 수정 후 계약 테스트 (`b18b7c3`) | 새 테스트가 결함을 잡음 | **7개 검사 XFAIL**: DEF-SW-01 2, DEF-SW-02 1, DEF-SW-04 4 (같은 실행에서 DEF-SW-03 1 XFAIL) | `evidence/after_fixes/prefix_b18b7c3_qttest_*.txt` |

수정 전 fuzz oracle(O1–O5)은 DEF-SW-04를 찾지 못했다. 새 oracle O6(Qt를 쓰지 않는 독립 참조 구현)는 같은 유형의 회귀(NC-8)를 1초 안에 찾는다.
