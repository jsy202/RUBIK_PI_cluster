# RUBIK Pi 3 Qt Digital Cluster · Post-project Performance Validation

## Project
RUBIK Pi 3 보드에서 동작하는 C++/Qt5 디지털 클러스터다. Arduino가 시리얼(9600 baud)로 보내는 RPM을 화면에 표시하고, 지연시간을 CSV로 기록해 분석한다.

## Architecture
```text
Sensor → Arduino → Serial → readSerialData() → acceptRpmSample() [t_in]
       → pending (30 Hz flush, 33 ms QTimer) → setText → TimestampLabel::paintEvent [t_frame] → CSV
```

## Original Research
연구 당시에는 다음을 구현했다. 아래 "Original Research Documentation"은 당시 README 원문이며 수정하지 않았다.

- Qt 클러스터 GUI
- 30 Hz UI throttle
- `paintEvent` 기반 지연 로깅
- Python/MATLAB 분석

포스터/논문의 RUBIK Pi 3 측정 수치는 이 저장소에 없다. 그래서 여기서는 인용하거나 재해석하지 않는다. 연구 당시 코드는 tag [`research-baseline`](https://github.com/jsy202/RUBIK_PI_cluster/tree/research-baseline) (`4d4e3f8`)에 있다.

## Post-project Validation (2026-10, 연구 종료 후)
요구 성능(30 Hz UI, 33.3 ms **soft budget**)을 측정 가능한 기준으로 정의하고, 하드웨어 없이 반복 가능한 입력으로 PASS/FAIL을 판정했다.

**측정 경계는 코드 기준으로 `sample acceptance → paintEvent`다.**
- 시작(`t_in`): 앱이 파싱된 샘플을 받아들인 시점
- 끝(`t_frame`): rpmLabel의 `paintEvent`가 backing store에 그리기를 마친 시점
- 포함: 30 Hz throttle 대기(0~33 ms)
- 포함하지 않음: 센서, UART 전송, **물리 디스플레이 표시**

→ [measurement_boundary.md](validation/measurement_boundary.md)

변경한 것:
- 시리얼 파서를 `rpmparser.h`로 그대로 옮겼다(동작 동일, 차등 테스트로 확인).
- 선택적 replay 입력 `CLUSTER_REPLAY_FILE`을 추가했다(기본 비활성).
- PASS/FAIL 분석기 `scripts/validate_latency.py`를 추가했다. 기존 분석 스크립트와 jitter 정의는 유지했다.

## Key Findings
**측정 환경: 개발 PC(x86_64) + Docker(Ubuntu 20.04, Qt 5.12.8) + Qt offscreen 플랫폼.** RUBIK Pi 3 하드웨어 결과가 아니며, 물리 디스플레이 지연도 아니다. 시나리오당 30 s × 3회.

| 시나리오 | p95 (`t_in→t_frame`) | 표시·로그되지 않은 샘플 | 판정 (p95 ≤ 33.3 ms) |
|---|---|---|---|
| 기존 시뮬레이션 50 Hz | 20 ms | 약 40 % | PASS |
| 입력 주기 = UI 주기 (33 ms) | 34 ms | 0 % | **FAIL** — 두 타이머의 위상 고정으로 throttle 대기가 거의 한 주기 |
| 1 kHz 버스트 | 2 ms | 약 97 % | PASS. 하지만 대부분의 입력이 버려짐 |
| 0/8000 교대 @20 ms | 21 ms | 약 79 % | PASS. 표시 값이 aliasing됨 |

- 지연 통계는 **표시된 샘플에 대해서만** 계산된다. 그래서 analysis에 "로그되지 않은 샘플 수"를 함께 보고하도록 했다.
- 같은 값이 반복되면 repaint가 없어 지연 표본이 생기지 않는다.

→ [performance_report.md](validation/performance_report.md)

## Input-rate Sweep (Post-project, 2026-10-02)
**환경: 개발 PC + Docker + Qt offscreen, replay 입력. 하드웨어 검증이 아니다.** 측정 구간은 `sample acceptance → paintEvent`이며, 각 조건 15 s × 3회를 실행했다.

| 입력 rate (명목) | 10 | 30 | 60 | 100 | 200 | 500 | 1000 (실측 938) |
|---|---|---|---|---|---|---|---|
| drop ratio | 0 % | 0 % | 48.4 % | 69.7 % | 84.8 % | 93.9 % | 96.8 % |
| p95 latency | 32 ms | **34 ms (FAIL)** | 17 ms | 10.3 ms | 5 ms | 3 ms | 2 ms |

입력 rate가 UI 주기(약 30 Hz)를 넘으면 **p95는 낮아지고 drop은 커진다.** 이는 성능 개선이 아니다. 지연 통계가 표시된 샘플에 대해서만 계산되기 때문이며, **latency 단독 지표가 처리 품질을 충분히 나타내지 못할 수 있음을 확인**한 결과다. 지연 측면의 최악 조건은 입력 주기가 UI 주기와 같은 30 Hz였다(위상 고정). → [rate_sweep/analysis.md](validation/rate_sweep/analysis.md)

## Verification
- **QtTest 16건**(Docker): 정상, 0, 음수 보정, int 경계, overflow, 잘못된 문자열, 빈 줄, CRLF, 분할 프레임, 다중 프레임, 반복값, 급변, 64/65바이트, 8 KiB 초과, 그리고 **원래 `readSerialData` 루프와의 차등 테스트**.
- negative control: 변이 2종이 각각 1건씩 실패로 검출되었다. `.trimmed()` 제거는 등가 변이라 검출되지 않았고, 이 사실을 기록했다.
- **pytest 8건**: 분석 스크립트의 통계, PASS/FAIL 판정, 종료 코드, 로그 누락 샘플 수 계산.
- 성능 시나리오 5종 × 3회(`scripts/run_perf_scenarios.sh`): 측정이며, CI에는 넣지 않았다.
- 문서: [requirements](validation/requirements.md) · [test cases](validation/test_cases.md) · [traceability](validation/traceability_matrix.md) · [limitations](validation/limitations.md)

## Test Result
| 테스트 | PASS | XFAIL | FAILED |
|---|---|---|---|
| QtTest 16 (+init/cleanup, Qt 집계 18) | 16 | 0 | 0 |
| pytest 8 | 8 | 0 | 0 |

로컬 실행 결과다. GitHub Actions 결과는 workflow `tests`를 참고한다.

## Limitations
- **RUBIK Pi 3, Arduino, 센서, 실제 디스플레이 없이** PC의 Docker offscreen 환경에서 측정했다. 연구 당시 결과와 같은 조건이 아니다.
- 시리얼 수신 경로의 성능은 측정하지 않았다(파서는 기능 테스트만 함). `CLUSTER_ANIMATE`도 측정하지 않았다.
- 33.3 ms는 hard real-time deadline이 아니라 soft budget이다.

## How to Run Tests (하드웨어 불필요)
```bash
docker build -f Dockerfile.test -t rubik-cluster-test . && docker run --rm rubik-cluster-test   # app 빌드 + QtTest
python3 -m pip install pytest && python3 -m pytest                                              # 분석 테스트
./scripts/run_perf_scenarios.sh                                                                 # (선택) 성능 시나리오, 약 8분
```

---

## Original Research Documentation (연구 당시 README 원문)


이 저장소는 **RUBIK PI 3 보드에서 Qt 기반 디지털 클러스터 시스템을 구현하고, 센서 데이터가 화면에 표시되기까지의 End-to-End(E2E) 지연시간을 측정하기 위한 프로젝트**입니다.

포스터/논문에서 사용한 전체 흐름은 다음과 같습니다.

```text
Sensor Data -> Arduino -> Serial Bus -> Debian/RUBIK PI 3 -> Qt Program -> CSV File -> MATLAB/Python Analysis
```

Qt 애플리케이션은 시리얼 포트로 들어오는 RPM 값을 받아 디지털 클러스터 화면에 표시하고, 데이터가 애플리케이션에 들어온 시점부터 Qt 위젯이 실제로 paint 된 시점까지의 시간을 CSV로 기록합니다. 이후 Python 또는 MATLAB 스크립트로 평균 지연시간, 지터, 최악 지연시간, 지연 분포를 분석할 수 있습니다.

## 주요 기능

- Qt 5 기반 디지털 클러스터 GUI
- 시리얼 통신 기반 RPM 데이터 수신
- RPM 값 기반 속도 표시
- 30Hz UI 갱신 제한
- E2E 지연시간 CSV 로깅
- 센서/아두이노 없이 실행 가능한 시뮬레이션 모드
- Python 분석 스크립트 제공
- MATLAB 분석 스크립트 제공
- Docker 기반 빌드 및 실행 환경 제공

## 프로젝트 구조

```text
.
├── ctest101.pro                  # qmake 프로젝트 파일
├── main.cpp                      # Qt 애플리케이션 진입점, 배경 리소스 설정
├── mainwindow.cpp                # 시리얼 수신, UI 갱신, CSV 로깅 핵심 로직
├── mainwindow.h                  # MainWindow 클래스 선언
├── mainwindow.ui                 # Qt Designer UI 파일
├── timestamplabel.h              # paint 완료 시점을 알리기 위한 QLabel 확장 클래스
├── resources.qrc                 # Qt 리소스 파일
├── images/                       # 클러스터 배경 이미지
├── scripts/
│   ├── analyze_latency.py         # Python 지연시간 분석 스크립트
│   └── analyze_latency.m          # MATLAB 지연시간 분석 스크립트
├── Dockerfile                    # Qt 빌드/실행용 Dockerfile
└── README.md
```

## 측정 정의

본 프로젝트에서 E2E 지연시간은 다음과 같이 정의합니다.

```text
T_E2E = T_frame - T_in
```

- `T_in`: RPM 데이터가 Qt 애플리케이션에 수신된 시점
- `T_frame`: 해당 RPM 값이 Qt GUI 위젯에 렌더링 완료된 시점
- `T_E2E`: 수신부터 화면 표시까지 걸린 전체 시간

앱은 `TimestampLabel`의 `paintEvent()`가 끝난 시점을 사용해 `T_frame`을 기록합니다. 따라서 단순히 `setText()`를 호출한 시간이 아니라, Qt가 실제 화면 갱신을 수행한 뒤의 시점을 기준으로 로그가 남습니다.

## 요구 환경

### 네이티브 빌드

Ubuntu/Debian 계열 기준으로 다음 패키지가 필요합니다.

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  qtbase5-dev \
  qt5-qmake \
  qtbase5-dev-tools \
  libqt5serialport5-dev
```

### Docker 빌드

Docker를 사용할 경우 로컬에 Qt 개발 패키지를 직접 설치하지 않아도 됩니다.

```bash
docker --version
```

## 빌드 방법

### 네이티브 빌드

```bash
qmake ctest101.pro
make -j"$(nproc)"
```

빌드가 성공하면 현재 디렉터리에 실행 파일이 생성됩니다.

```text
digital_cluster
```

### Docker 빌드

```bash
docker build -t digital-cluster .
```

Dockerfile은 두 단계로 구성되어 있습니다.

- build stage: Ubuntu 20.04에서 Qt 5 프로젝트 빌드
- runtime stage: Debian bullseye slim에 실행에 필요한 Qt 런타임만 포함

## 실행 방법

### 1. 실제 RUBIK PI 3 / Arduino / 센서 환경

기본 시리얼 설정은 다음과 같습니다.

- Baud rate: `9600`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`
- Flow control: `None`

실행 예시:

```bash
CLUSTER_SERIAL_PORT=/dev/ttyACM0 CLUSTER_BAUD=9600 ./digital_cluster
```

USB-Serial 장치명이 `/dev/ttyUSB0`인 경우:

```bash
CLUSTER_SERIAL_PORT=/dev/ttyUSB0 CLUSTER_BAUD=9600 ./digital_cluster
```

`CLUSTER_SERIAL_PORT`를 지정하지 않으면 Qt가 감지한 첫 번째 시리얼 포트를 사용합니다. 실험 재현성을 위해서는 포트를 명시하는 것을 권장합니다.

### 2. 노트북/PC에서 시뮬레이션 실행

RUBIK PI 3, Arduino, 센서가 없어도 GUI와 로깅 파이프라인을 확인할 수 있습니다.

```bash
CLUSTER_SIMULATE=1 ./digital_cluster
```

시뮬레이션 모드는 내부적으로 주기적인 RPM 값을 생성합니다. 이 모드에서도 실제 실행과 동일하게 CSV 로그가 생성되고 분석 스크립트를 사용할 수 있습니다.

### 3. Docker로 GUI 실행

Linux X11 환경에서 Docker 컨테이너로 실행하는 예시입니다.

```bash
xhost +local:docker

docker run --rm -it \
  -e DISPLAY="$DISPLAY" \
  -e CLUSTER_SIMULATE=1 \
  -e QT_X11_NO_MITSHM=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  digital-cluster
```

실제 시리얼 장치를 연결해서 Docker로 실행하려면 장치를 컨테이너에 넘겨야 합니다.

```bash
xhost +local:docker

docker run --rm -it \
  -e DISPLAY="$DISPLAY" \
  -e CLUSTER_SERIAL_PORT=/dev/ttyACM0 \
  -e CLUSTER_BAUD=9600 \
  -e QT_X11_NO_MITSHM=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  --device=/dev/ttyACM0 \
  digital-cluster
```

## 환경변수

| 변수 | 기본값 | 설명 |
| --- | --- | --- |
| `CLUSTER_SERIAL_PORT` | 첫 번째 감지 포트 | 사용할 시리얼 포트 경로 |
| `CLUSTER_BAUD` | `9600` | 시리얼 baud rate |
| `CLUSTER_SIMULATE` | 비활성 | `1` 또는 `true`로 설정하면 시뮬레이션 RPM 입력 사용 |
| `CLUSTER_ANIMATE` | 비활성 | `1` 또는 `true`로 설정하면 RPM 표시 애니메이션 사용 |

지연시간 측정 시에는 `CLUSTER_ANIMATE`를 끄는 것을 권장합니다. 애니메이션을 켜면 표시 값이 중간값을 거치므로 “수신된 값이 처음 화면에 표시된 시점”을 엄밀하게 비교하기 어려워질 수 있습니다.

## CSV 로그

앱을 실행하면 실행 위치에 다음 형식의 CSV 파일이 생성됩니다.

```text
latency_metrics_YYYYMMDD_hhmmss.csv
```

CSV 컬럼은 다음과 같습니다.

| 컬럼 | 의미 |
| --- | --- |
| `seq` | 수신 샘플 번호 |
| `source` | `serial` 또는 `simulation` |
| `t_in_ms` | 애플리케이션이 데이터를 수신한 시점 |
| `t_frame_ms` | Qt 위젯 paint 완료 시점 |
| `e2e_latency_ms` | `t_frame_ms - t_in_ms` |
| `raw_rpm` | 시리얼/시뮬레이션에서 받은 원본 RPM |
| `rpm` | 보정 후 화면에 표시한 RPM |

예시:

```csv
seq,source,t_in_ms,t_frame_ms,e2e_latency_ms,raw_rpm,rpm
1,serial,120,131,11,2500,2500
2,serial,140,151,11,2512,2512
```

## 분석 방법

### Python 분석

```bash
python3 scripts/analyze_latency.py latency_metrics_YYYYMMDD_hhmmss.csv --out-dir analysis_output
```

출력되는 주요 지표:

- 샘플 수
- 평균 지연시간
- 지터: 표준편차
- 최소 지연시간
- 최악 지연시간
- p50, p95, p99 지연시간

`matplotlib`가 설치되어 있으면 다음 이미지도 생성됩니다.

- `analysis_output/latency_trend.png`
- `analysis_output/latency_distribution.png`

`matplotlib` 설치:

```bash
python3 -m pip install matplotlib
```

### MATLAB 분석

MATLAB에서 다음 명령을 실행합니다.

```matlab
analyze_latency("latency_metrics_YYYYMMDD_hhmmss.csv")
```

MATLAB 스크립트는 평균 지연시간, 지터, 최악 지연시간을 출력하고 trend/distribution 그래프를 저장합니다.

## 실험 절차 예시

1. Arduino가 RPM 값을 줄 단위 정수 문자열로 송신하도록 설정합니다.

   ```text
   2500\n
   2512\n
   2498\n
   ```

2. RUBIK PI 3에서 시리얼 장치를 확인합니다.

   ```bash
   ls /dev/ttyACM*
   ls /dev/ttyUSB*
   ```

3. 앱을 실행합니다.

   ```bash
   CLUSTER_SERIAL_PORT=/dev/ttyACM0 CLUSTER_BAUD=9600 ./digital_cluster
   ```

4. 충분한 시간 동안 데이터를 수집합니다.

5. 생성된 CSV를 분석합니다.

   ```bash
   python3 scripts/analyze_latency.py latency_metrics_20260612_141545.csv --out-dir analysis_output
   ```

6. 평균 지연시간, 지터, 최악 지연시간, 분포 그래프를 포스터/논문 결과와 비교합니다.

## 구현 로직 요약

### 시리얼 수신

`mainwindow.cpp`의 `readSerialData()`는 시리얼 포트에서 들어온 바이트를 버퍼에 저장하고, 줄바꿈 문자 `\n` 기준으로 RPM 샘플을 분리합니다. 너무 긴 라인은 무시해 잘못된 입력이 UI 갱신 루프를 방해하지 않도록 했습니다.

### UI 갱신

수신된 RPM은 바로 화면에 반영하지 않고, 30Hz 타이머에서 `flushPendingRpm()`을 통해 UI에 적용합니다. 이는 너무 높은 빈도의 입력이 들어와도 UI가 과도하게 갱신되지 않도록 하기 위한 구조입니다.

### paint 완료 시점 기록

`TimestampLabel`은 `QLabel`을 상속해 `paintEvent()`가 끝난 뒤 `painted()` signal을 emit합니다. `MainWindow`는 이 signal을 받아 `t_frame_ms`를 기록하고 CSV에 E2E latency를 저장합니다.

### 시뮬레이션 입력

`CLUSTER_SIMULATE=1`을 설정하면 실제 시리얼 포트를 열지 않고 내부 타이머가 RPM 값을 생성합니다. 이 기능은 노트북에서 GUI, CSV 로깅, 분석 스크립트가 정상 동작하는지 확인할 때 사용합니다.

## 문제 해결

### `qmake: command not found`

Qt 개발 도구가 설치되지 않은 상태입니다.

```bash
sudo apt install -y qt5-qmake qtbase5-dev qtbase5-dev-tools
```

### `libQt5SerialPort.so.5: cannot open shared object file`

Qt SerialPort 런타임 라이브러리가 없습니다.

```bash
sudo apt install -y libqt5serialport5 libqt5serialport5-dev
```

### `Port Err`가 화면에 표시됨

시리얼 포트를 찾지 못한 상태입니다.

확인 명령:

```bash
ls /dev/ttyACM*
ls /dev/ttyUSB*
```

실행 예:

```bash
CLUSTER_SERIAL_PORT=/dev/ttyUSB0 ./digital_cluster
```

### 시리얼 포트 권한 오류

현재 사용자가 `dialout` 그룹에 없을 수 있습니다.

```bash
sudo usermod -aG dialout "$USER"
```

이후 로그아웃/로그인하거나 재부팅합니다.

### Docker GUI 창이 뜨지 않음

X11 권한 또는 DISPLAY 전달 문제가 원인일 수 있습니다.

```bash
echo "$DISPLAY"
xhost +local:docker
```

Wayland 세션에서도 XWayland가 동작하면 실행될 수 있지만, 환경에 따라 추가 설정이 필요할 수 있습니다.

### Docker에서 OpenGL 관련 경고가 표시됨

다음과 같은 경고가 뜰 수 있습니다.

```text
libGL error: failed to load driver
```

단순 Qt Widgets GUI 실행에는 치명적이지 않을 수 있습니다. 앱 창이 뜨고 CSV가 생성되면 로직은 동작 중입니다.

## 재현성 관련 주의

포스터에 제시된 평균 지연시간과 지터 값은 특정 하드웨어, OS, Docker 런타임, 디스플레이 환경, 센서 입력 주기에서 측정된 결과입니다. 다른 노트북이나 보드에서 실행하면 수치는 달라질 수 있습니다.

이 저장소는 특정 수치를 하드코딩하지 않고, 동일한 방식으로 데이터를 수집하고 분석할 수 있도록 구성되어 있습니다.

## 권장 커밋 제외 파일

다음 파일들은 빌드 또는 실행 중 생성되는 산출물이므로 저장소에 올리지 않는 것을 권장합니다.

- `*.o`
- `Makefile`
- `moc_*.cpp`
- `moc_*.o`
- `qrc_*.cpp`
- `qrc_*.o`
- `ui_*.h`
- `.qmake.stash`
- `*.pro.user`
- `digital_cluster`
- `latency_metrics_*.csv`
- `analysis_output/`

`.dockerignore`에는 Docker 빌드 컨텍스트에서 불필요한 산출물이 제외되도록 설정되어 있습니다.
