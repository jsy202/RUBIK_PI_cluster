# Limitations — Post-project Software Verification

이 디렉터리의 모든 결과는 **개발 PC(x86_64, Linux 6.8)의 Docker 컨테이너(Ubuntu 20.04, Qt 5.12.8, GCC 9.3, clang 10)**에서 실행한 것이다. RUBIK Pi 3 하드웨어를 다시 사용할 수 없었고, 사용하지 않았다.

## 이 검증이 말하지 않는 것

| 주장하지 않는 것 | 이유 |
|---|---|
| RUBIK Pi 3에서 후속 검증을 완료했다 | 하드웨어 없이 진행했다 |
| 하드웨어 성능을 다시 검증했다 | 성능은 측정하지 않았다. MainWindow 테스트는 지연 **값**에 대해 아무것도 단언하지 않는다(`e2e = t_frame − t_in`, `e2e ≥ 0` 형식만 확인) |
| physical display latency | 측정 구간 밖이다(`../measurement_boundary.md`). offscreen 플랫폼은 화면에 그리지 않는다 |
| sensor-to-display E2E | 센서, Arduino, UART가 없다 |
| UART를 포함한 전체 latency | pty(가상 터미널)로 앱 내부의 `QSerialPort → readSerialData` 경로만 실행했다. pty는 UART가 아니고, baud rate와 전송 시간이 의미가 없다 |
| fuzzing으로 하드웨어 안정성을 입증했다 | fuzz 대상은 `rpmparser.h`의 순수 함수뿐이다. MainWindow, Qt event loop, 드라이버, 보드는 대상이 아니다 |
| PC 결과가 실제 RUBIK Pi 성능을 대표한다 | 이 디렉터리에는 성능 수치가 없다. 기존 PC/Docker 성능 측정(`../performance_report.md`)도 Appendix일 뿐이며 RUBIK Pi 결과와 비교하지 않는다 |

## 방법별 한계

| 항목 | 한계 |
|---|---|
| Coverage | gcov의 branch는 소스 결정문이 아니라 **컴파일러 arc** 단위로 센다. Qt 임시 객체나 문자열 연산이 만든 arc도 포함된다. 예외 전용(throw) arc와 unreachable arc는 제외했다. 제외 옵션은 실행 기록(.gcda)이 있는 번역 단위에서만 동작하므로, 테스트가 실행하지 않는 `main.cpp`(Qt 부트스트랩, 배경 이미지)는 범위에서 빼고 따로 적었다 |
| Coverage 미달 분기 | 남은 미실행 분기는 다음과 같다. ① `setupSerialPort`의 포트 자동 탐지와 `Port Err`: 결과가 host의 serial 장치 구성에 따라 달라 재현 가능한 테스트를 만들 수 없다. ② `ui->…` null 검사: `.ui`가 항상 위젯을 만들기 때문에 도달할 수 없다. ③ `CLUSTER_SIMULATE=true` 같은 env 문자열 변형, `CLUSTER_BAUD`·`CLUSTER_REPLAY_INTERVAL_MS` 기본값: 검증 가치가 낮아 테스트를 추가하지 않았다 |
| Mutation | 자동 mutation 도구가 아니라 **손으로 고른 24개(수정 후 30개)**다. 모든 결함을 대표하지 않는다. 동등(equivalent) 판정은 코드 리뷰와 실행 결과에 근거했으며, Qt 5.12 의미론에서만 확인했다 |
| Fuzz | 커버리지 유도 fuzzing(libFuzzer)을 10분 동안 단일 프로세스로 돌렸다. 입력 최대 12,000바이트다. 수정 전 oracle(O1–O5, baseline 차등)은 값을 잘못 받아들이는 결함을 잡지 못했다. DEF-SW-04는 표 테스트로 찾았다. 수정 후 추가한 O6(독립 참조 구현)도 결국 **내가 작성한 계약**과 비교할 뿐이다. 수정 전후 실행의 입력 수(1,203,649 / 511,839)는 harness가 달라 직접 비교할 수 없다 |
| Sanitizer | ASan과 UBSan만 사용했다. Qt 라이브러리 자체는 계측되지 않았다(시스템 패키지). LeakSanitizer는 테스트 실행에서 껐다(Qt와 fontconfig의 프로세스 수명 할당 때문). fuzz 실행에서는 켰다 |
| ASan 실행 환경 | GCC 9와 clang 10의 ASan 런타임은 mmap ASLR 엔트로피가 높은 커널(이 PC의 Linux 6.8)에서 `main()` 이전, 런타임 초기화 중에 간헐적으로 SIGSEGV가 난다(관측: 30회 중 8회, 스택은 `__asan::AsanInitInternal`). 앱 결함이 아니다. `setarch -R`(ASLR 끔)로 실행하면 30회 중 0회였다. 이를 위해 Docker를 `--security-opt seccomp=unconfined`로 실행한다 |
| 테스트 환경 | Qt `offscreen` 플랫폼에서 `repaint()`를 직접 불러 paintEvent를 만들었다. 실제 윈도 시스템의 expose/compositor 동작은 재현하지 않았다 |
| Qt 버전 | 모든 결과는 Qt 5.12.8 기준이다. RUBIK Pi 연구 환경의 Qt 버전에서 DEF-SW-04가 같은지는 확인하지 않았다 |
| 결함 수정 | DEF-SW-01, 02, 04는 수정했다(아래 production 변경). DEF-SW-03은 **수정하지 않았다.** 연구 당시 측정 의미와 직접 관련되기 때문이며, 테스트에서 `QEXPECT_FAIL`로 고정했다 |
| DEF-SW-03 (Known Measurement Limitation) | 동일한 값의 샘플이 들어온 뒤 관계없는 repaint가 일어나면, 그 시점이 `t_frame`으로 기록될 수 있다. host 환경에서 **실제로 재현했다.** 연구 당시 원본 CSV가 저장소에 없어서 이 동작이 연구 결과(평균 10.38 ms, 최대 21 ms)에 얼마나 영향을 줬는지는 **판단할 수 없다.** 따라서 Historical Result가 무효라고 단정하지 않고, 영향이 없었다고도 단정하지 않는다 |
| 수정의 검증 범위 | 수정은 host(Qt 5.12.8, x86-64)에서만 검증했다. RUBIK Pi에서 수정된 코드를 실행하지 않았다. 따라서 수정 후 코드는 연구 당시 측정한 코드와 **다르다.** Historical Result는 `research-baseline` 코드의 결과다 |
| 상한 911,420,367 | 차량 RPM 범위가 아니라 현재 속도 공식과 int 타입이 안전하게 처리하는 최대값이다. 실제 센서 범위에 맞는 상한은 정의하지 않았다 |

## Production 코드 변경

- 결함 발견 단계(`f9e211c`)까지는 **없다.** MainWindow는 기존 입력(환경변수, replay 파일, pty)과 Qt meta-object 시스템(`QMetaObject::invokeMethod`로 private slot 호출)으로만 구동했다.
- 결함 수정 단계에서 바꾼 파일은 2개다:
  - `rpmparser.h`: `parseLine()`(줄 전체 형식 검사), `kMaxRpm`(상한), 주석. DEF-SW-04, DEF-SW-01 수정
  - `mainwindow.cpp`: `flushPendingRpm()`의 2줄(64비트 delta, `2 * clamp(delta, 30, 90)`). DEF-SW-02 수정
- 바꾸지 않은 파일: `mainwindow.h`, `timestamplabel.h`, `main.cpp`, `mainwindow.ui`, `ctest101.pro`. 측정 코드(`t_in`, `t_frame`, CSV 형식)도 그대로다.
