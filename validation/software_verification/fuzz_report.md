# Parser Fuzz / Robustness Report — Post-project Software Verification

> 환경: 개발 PC(x86_64, Linux 6.8) + Docker(`Dockerfile.verify`: Ubuntu 20.04, clang 10 libFuzzer, Qt 5.12.8). 단일 프로세스로 실행했다.
> RUBIK Pi 3 결과가 아니며, 하드웨어 안정성에 대해서는 아무것도 말하지 않는다.

## 대상과 범위

| 항목 | 내용 |
|---|---|
| fuzz 대상 함수 | `RpmParser::consume(buffer, chunk, 64, 8192)`, `RpmParser::correct(int)` (`rpmparser.h`). 시리얼 바이트가 처음 거치는 외부 입력 경로다 |
| 대상 아님 | MainWindow, Qt event loop, QSerialPort, 드라이버, UART, 보드 |
| harness | [`tests/fuzz/fuzz_rpmparser.cpp`](../../tests/fuzz/fuzz_rpmparser.cpp). 첫 바이트로 입력을 1~4개의 read로 나누고, 나머지를 시리얼 바이트 스트림으로 쓴다. production 코드는 바꾸지 않았다 |
| 방법 | **libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer** (`-fsanitize=fuzzer,address,undefined,float-cast-overflow -fno-sanitize-recover=all`), LeakSanitizer 켬, `-max_len=12000 -len_control=0` |
| oracle (위반 시 `abort()`) | O1: 호출 후 버퍼에 `\n`이 없고 크기 ≤ 8 KiB. O2: 반환 값 수 ≤ 줄 수. O3: research-baseline 원래 루프와 출력·버퍼가 같다(차등). O4: 8 KiB 이하일 때 read 분할과 무관하게 결과가 같다. O5: `correct()` ≥ 0이고 음수가 아닌 값은 유지한다 |
| seed corpus | [`tests/fuzz/corpus/`](../../tests/fuzz/corpus/) 14개. 요청된 입력군을 포함한다: 0, 일반 RPM, INT 경계, 반복 값, 0↔8000 급변, 빈 줄/공백, 영문, 숫자+영문, 음수, int overflow, 100자리 줄, partial frame, CR/CRLF/CRCRLF, NUL·상위 바이트, 9 KiB 무종결 입력 |

## 결과 (최종 실행: `scripts/sw_verify.sh fuzz 600`)

| 항목 | 값 |
|---|---:|
| 실행 시간 | 601 s |
| 총 입력 수 (executed units) | **1,203,649** |
| 평균 속도 | 2,002 exec/s |
| corpus에 새로 추가된 입력 | 3,040 |
| crash | **0** |
| sanitizer violation (ASan/UBSan/LSan) | **0** |
| oracle violation (O1–O5) | **0** |
| fuzzer가 찾은 실제 defect | **0** |
| fuzzer exit code | 0 |

Evidence: [`evidence/fuzz_600s_trimmed.txt`](evidence/fuzz_600s_trimmed.txt) (진행 로그를 줄인 사본. 시작 설정과 최종 `stat::` 줄은 원문 그대로다).

## Deterministic robustness 테스트 (QtTest, 같은 입력군)

`tst_rpmparser::robustnessInputClasses`는 13행, 기존 parser 테스트는 16건이다. sanitizer 빌드(ASan + UBSan, GCC 9)로도 실행했고 **위반은 0건**이다([`evidence/sanitize_parser.txt`](evidence/sanitize_parser.txt)).

이 표 테스트가 **실제 결함 1건을 찾았다**(fuzzer는 찾지 못했다):

- **DEF-SW-04**: Qt 5.12.8의 `QString::toInt()`는 문자열 안의 NUL에서 파싱을 멈춘다. 그래서 `"7\0abc\n"`은 7로, `"25\0" "00\n"`은 25로 **수용된다.** 반면 `"12a\n"`은 거부된다. REQ-IN-002와 어긋나며, `QEXPECT_FAIL`로 고정했고 수정하지 않았다.
- fuzzer가 이 결함을 찾지 못한 이유: oracle이 "baseline과 같은가"(O3)와 구조 불변식만 확인한다. 값이 맞는지는 보지 않는다. baseline 루프도 같은 `toInt`를 쓰므로 차등 검사로는 드러나지 않는다.

같은 표가 기록한 기존 동작(결함으로 분류하지 않음): bare CR(`"2500\r3000\n"`)은 줄 구분자가 아니라서 두 값이 모두 버려진다. README에 적힌 프로토콜(줄마다 `\n`)과는 맞는다.

## 검증 수단 자체의 확인 (negative control)

| 넣은 결함 (임시 사본) | 결과 |
|---|---|
| 8 KiB 상한 제거 | 고친 설정: **1초 이내 검출**. seed 포함 시 O1 위반, 9 KiB seed를 빼도 O3 위반으로 검출했다. 처음 설정(기본 `len_control`): **120초 동안 검출 못 함**. 입력 길이 상한이 2,226바이트까지만 올라 8 KiB 경로에 도달하지 못했다 |
| `buffer.left(nl)` → 70바이트 앞부터 복사(범위 밖 읽기) | **0초에 검출**: ASan `heap-use-after-free` |

첫 설정의 약점은 negative control로 발견했다. 이후 `-len_control=0`과 9 KiB seed를 추가하고, 위 최종 결과를 그 설정으로 다시 측정했다. 첫 설정의 10분 실행(1,038,553개 입력, crash 0)은 버퍼 상한 경로를 거의 시험하지 못했으므로 **결과로 인용하지 않는다.**

## CI

CI(`sw-verify` job)는 같은 harness로 **60초 smoke**만 실행한다. 10분 실행은 로컬에서 한다. CI의 입력 수는 runner마다 다르므로 문서에 수치로 적지 않는다.

## 한계

- 단일 프로세스로 10분만 돌렸다. 장시간 실행이나 분산 fuzzing은 하지 않았다.
- 값의 정확성은 oracle이 정의한 범위(baseline 동일성, 분할 불변성, 보정 규칙)까지만 본다.
- MainWindow 이후의 경로(UI 갱신, 속도 변환)는 fuzz 대상이 아니다. 그 경로의 UB(DEF-SW-01/02)는 경계값 테스트와 UBSan으로 찾았다.
- ASan 런타임 초기화 문제 때문에 `setarch -R`(ASLR 끔)로 실행했다(`limitations.md`).
