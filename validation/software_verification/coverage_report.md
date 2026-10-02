# Coverage Report — Post-project Software Verification

> 환경: 개발 PC + Docker(`Dockerfile.verify`: Ubuntu 20.04, Qt 5.12.8, GCC 9.3 `--coverage`, gcovr 7.2), Qt `offscreen`.
> RUBIK Pi 3에서 실행한 결과가 아니다. 실행: `scripts/sw_verify.sh coverage`.
> 원본 출력: 수정 전 [`evidence/`](evidence/), 수정 후 [`evidence/after_fixes/`](evidence/after_fixes/).

## 측정 범위

| 포함 (직접 작성한 application logic) | 제외 |
|---|---|
| `rpmparser.h` (parser, 입력 검증, 버퍼링, RPM 보정) | Qt framework 헤더, system 헤더 |
| `mainwindow.cpp`, `mainwindow.h` (입력 소스, pending/30 Hz 갱신, 속도 변환, CSV 계측) | `moc_*.cpp`, `ui_*.h` (생성 코드) |
| `timestamplabel.h` (paintEvent 완료 알림) | 테스트 코드 자체 |
| `scripts/analyze_latency.py`, `scripts/validate_latency.py` (측정 분석, 별도 표) | `main.cpp`: 배경 이미지와 창 표시만 하는 Qt 부트스트랩이다. 어떤 테스트도 실행하지 않는다(15줄, 함수 1개, 0%). 아래 "한계" 참조 |

Branch 설정: `--exclude-throw-branches --exclude-unreachable-branches`. gcov는 소스의 결정문이 아니라 **컴파일러 arc** 단위로 branch를 센다. 따라서 Qt 문자열·임시 객체 호출이 만든 arc도 분모에 들어간다.

## 단계별 요약 (C++, in-scope)

| 단계 | Line | Function | Branch |
|---|---:|---:|---:|
| Before hardening (기존 테스트만, `main` 977bd2f) | 8.1% (16/198) | 10.5% (2/19) | 약 7% |
| After hardening, 결함 수정 전 (`f9e211c`) | 97.0% (192/198) | 100.0% (19/19) | 84.4% (195/231) |
| **After fixes (DEF-SW-01/02/04 수정 후, `dae0620`)** | **97.1% (202/208)** | **100.0% (20/20)** | **85.9% (219/255)** |

수정 후 분모가 커진 이유는 production 코드가 늘었기 때문이다(`RpmParser::parseLine` 추가, 10 lines, 1 function, branch arc 24개). 수치를 유지하려고 추가한 테스트는 없다. 수정 후 늘어난 테스트는 결함 수정의 계약을 확인하는 것뿐이다(형식 계약 표, 상한, overflow).

### 수정 후 모듈별 (`dae0620`)

| Module | Line | Function | Branch |
|---|---:|---:|---:|
| RPM parser / input validation (`consume`, `parseLine`, `correct`) | 100.0% (26/26) | 100.0% (3/3) | 97.6% (41/42) |
| Input sources (serial, replay, simulation) | 91.2% (62/68) | 100.0% (6/6) | 76.8% (76/99) |
| Pending sample + 30 Hz UI update | 100.0% (51/51) | 100.0% (4/4) | 83.3% (30/36) |
| State / data conversion (speed, RPM correction) | 100.0% (12/12) | 100.0% (2/2) | 80.0% (4/5) |
| Measurement / CSV logging | 100.0% (17/17) | 100.0% (3/3) | 100.0% (7/7) |
| Lifecycle (constructor setup, destructor) | 100.0% (34/34) | 100.0% (2/2) | 92.4% (61/66) |

Python 측정 분석 coverage는 수정 전후가 같다(line 92/117, branch 27/30). Python 코드는 바꾸지 않았다.

## 수정 전 결과 (`f9e211c`, C++, QtTest parser + QtTest MainWindow)

| | Line | Function | Branch |
|---|---:|---:|---:|
| **Total (in scope)** | **97.0% (192/198)** | **100.0% (19/19)** | **84.4% (195/231)** |

### 모듈별

| Module | Line | Function | Branch |
|---|---:|---:|---:|
| RPM parser / input validation (`RpmParser::consume`, `correct`) | 100.0% (16/16) | 100.0% (2/2) | 94.4% (17/18) |
| Input sources (serial, replay, simulation) | 91.2% (62/68) | 100.0% (6/6) | 76.8% (76/99) |
| Pending sample + 30 Hz UI update | 100.0% (51/51) | 100.0% (4/4) | 83.3% (30/36) |
| State / data conversion (speed, RPM correction) | 100.0% (12/12) | 100.0% (2/2) | 80.0% (4/5) |
| Measurement / CSV logging | 100.0% (17/17) | 100.0% (3/3) | 100.0% (7/7) |
| Lifecycle (constructor setup, destructor) | 100.0% (34/34) | 100.0% (2/2) | 92.4% (61/66) |

모듈은 함수 단위로 나눴다(`scripts/coverage_modules.py`). 줄은 시작 줄이 가장 가까운 앞쪽 함수에 속한다.

### Python 측정 분석 (`pytest-cov`, branch 포함)

| File | Line | Branch |
|---|---:|---:|
| `scripts/validate_latency.py` | 94.9% (37/39) | 92.9% (13/14) |
| `scripts/analyze_latency.py` | 70.5% (55/78) | 87.5% (14/16) |
| **Total** | **78.6% (92/117)** | **90.0% (27/30)** |

`analyze_latency.py`에서 실행되지 않은 부분은 대부분 `write_plots()`(L56–78)다. 컨테이너에 matplotlib이 없어 그래프 생성이 건너뛰어진다. 그래프 출력은 테스트 대상으로 삼지 않았다.

## 기존 테스트만 있을 때 (이번 작업 전)

기존 QtTest(parser 16건)는 `rpmparser.h`만 컴파일한다. `mainwindow.cpp`와 `timestamplabel.h`는 **어떤 테스트도 실행하지 않았다.**

| | Line | Function | Branch |
|---|---:|---:|---:|
| `rpmparser.h` (parser-only 빌드) | 100% (16/16) | 100% (2/2) | 100% (17/17) |
| MainWindow, TimestampLabel | 0% (0/182) | 0% (0/17) | 0% |
| **In-scope total** | **8.1% (16/198)** | **10.5% (2/19)** | **약 7%** |

Branch total을 "약"으로 적은 이유: 실행 기록이 없는 번역 단위에서는 gcovr가 throw arc를 걸러낼 수 없다. 그래서 같은 필터를 적용한 분모는 현재 실행의 값(MainWindow 쪽 213 arc)을 썼다. 이 때문에 parser는 parser-only 빌드에서 17 arc, MainWindow TU와 합친 현재 빌드에서는 18 arc로 분모가 다르다.

## 커버리지를 올리기 위해 추가한 테스트

coverage 수치를 맞추려고 추가한 테스트는 없다. 미실행 분기를 하나씩 검토한 결과, **동작상 의미가 있는 분기 1개**에만 테스트를 추가했다.

| 미실행 분기 (추가 전) | 판단 | 조치 |
|---|---|---|
| 생성자 L27 `m_csvFile.open()` 실패, `logEvent` L165 `!m_csv.device()` | CSV를 만들 수 없을 때 앱이 계속 동작하는지 확인한 적이 없다 | `displayWorksWhenCsvCannotBeCreated` 추가 (REQ-MEAS-003) |
| `setupSerialPort` 포트 자동 탐지, `Port Err` (L196–205) | 결과가 host의 serial 장치 구성에 따라 달라 재현 가능한 테스트를 만들 수 없다 | 미검증으로 기록 |
| `if (ui->rpmLabel)`, `if (ui->speedLabel)`, `qobject_cast<TimestampLabel*>` 실패 | `.ui`가 항상 위젯을 만들어서 도달할 수 없다 | 없음 |
| `CLUSTER_SIMULATE=true`, `CLUSTER_BAUD`·`CLUSTER_REPLAY_INTERVAL_MS` 미설정 시 기본값 | 환경변수 문자열 변형이라 결함 가능성 대비 가치가 낮다 | 없음 |
| 소멸자의 serial/CSV `isOpen()` 거짓 경로 | 정리 코드라 관찰 가능한 동작이 없다 | 없음 |

참고로 coverage가 아니라 **mutation**이 찾은 테스트 공백이 1건 있었다. `validate_latency`의 판정 경계(`p95 == budget`)다. 이 경계는 line/branch coverage가 이미 100%였는데도 검증되지 않고 있었다([`mutation_report.md`](mutation_report.md) P01).

## 한계

- 이 수치는 **host 테스트가 production 코드의 어느 부분을 실행했는지**만 보여준다. RUBIK Pi에서의 동작이나 성능과는 관계가 없다.
- MainWindow 테스트는 `QMetaObject::invokeMethod`로 private slot을 직접 호출한다. Qt timer와 event loop 순서는 일부 테스트(`replayPlaysAtConfiguredIntervalAndStops`, `serialPathViaPseudoTerminal`)에서만 실제로 돌린다.
- HTML 상세 보고서(`.verify-out/html/`)는 저장소에 넣지 않았다. CI artifact `sw-verify-results`로 받을 수 있다.
