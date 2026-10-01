# Measurement Boundary — RUBIK Pi Qt Digital Cluster

코드를 기준으로 **무엇을 측정했고 무엇을 측정하지 않았는지** 정리한다(정적 분석, B).
라인 번호는 validation 브랜치 기준이며, 측정 로직은 research-baseline(`4d4e3f8`)과 같다. parser 추출 외에는 변경하지 않았다.

## 1. 측정 구간 한눈에 보기

```text
 센서 → Arduino ADC/처리 → Serial.print → UART 9600 baud → USB-Serial/드라이버 → Qt event loop(readyRead)
 └──────────────────────────── 측정하지 않음 ────────────────────────────┘
                                                   │
 readSerialData(): readAll + RpmParser::consume ───┤
                                                   ▼
 acceptRpmSample()  t_in = m_timerBase.elapsed()   (mainwindow.cpp L179)    ◀── 측정 시작
        │  pending 슬롯에 덮어쓰기 (이전 pending 샘플은 버려짐)
        ▼
 flushPendingRpm()  QTimer 33 ms, Qt::PreciseTimer (L42–43)  → pending 대기 0~33 ms
        │  setDisplayRpm(): 값이 바뀌었을 때만 setText (L87)
        ▼
 Qt event loop → TimestampLabel::paintEvent() → QLabel::paintEvent() 반환
        │  emit painted()  (timestamplabel.h L17)
        ▼
 lambda: t_frame = m_timerBase.elapsed()  (L62)                            ◀── 측정 끝
 └─ logEvent(): CSV 1행 기록 + flush
        ▼
 backing store → 윈도 시스템 / compositor → GPU → vsync / scan-out → 패널 응답
 └──────────────────────────── 측정하지 않음 ────────────────────────────┘
```

## 2. 항목별 정의

| 항목 | 코드 기준 실제 의미 | 비고 |
|---|---|---|
| `t_in` | `acceptRpmSample()`이 호출된 순간의 `QElapsedTimer` 값이다. **시리얼 바이트가 OS에 도착한 시점이 아니다.** `readyRead` 처리 → `readAll()` → 줄 파싱이 끝난 뒤의 시점이다 | 한 번의 `readAll()`에 여러 줄이 있으면 모두 거의 같은 `t_in`을 갖는다 |
| `t_frame` | `TimestampLabel::paintEvent()`에서 `QLabel::paintEvent()`가 반환된 직후, 즉 rpmLabel이 윈도의 backing store에 그려진 시점 | **물리 화면 표시 시점이 아니다** |
| `e2e_latency_ms` | `t_frame − t_in` | 정수 ms |
| paintEvent의 의미 | 위젯 내용을 Qt의 backing store(메모리)에 래스터화하는 것. 이후 화면 반영(flush, 합성, vsync)은 포함되지 않는다 | – |
| physical display | **포함되지 않음** | 측정하려면 카메라나 포토다이오드 같은 외부 계측이 필요하다 |
| 30 Hz throttle | `kUiIntervalMs = 1000 / 30 = 33` (정수 나눗셈) → 33 ms 주기, 약 30.3 Hz. `Qt::PreciseTimer` | 문서의 "33.3 ms"는 이상값이고, 코드의 타이머 주기는 33 ms다 |
| pending 대기 | 샘플은 다음 flush까지 0~33 ms 대기하고, 이 시간은 **E2E에 포함된다** | 설계 의도(과도한 UI 갱신 방지) |
| 샘플 덮어쓰기 | flush 사이에 여러 샘플이 들어오면 마지막 샘플만 표시하고 로그로 남긴다. **덮어쓰인 샘플은 표시도, 로깅도 되지 않는다** | CSV `seq`의 공백으로 개수를 계산할 수 있다 |
| 같은 값 반복 | 표시 값이 그대로면 `setText`가 호출되지 않아 paint가 없고, **그 샘플은 로그로 남지 않는다** | 정속 구간의 로그 수가 줄어든다 |
| 다른 원인의 repaint | 로그 가드(`m_frameSeq != m_lastLoggedSeq`)는 flush 뒤 첫 paint 1회만 기록한다. 아직 로그되지 않은 seq에 대해 다른 원인(expose 등)으로 repaint가 일어나면, 그 시점이 `t_frame`으로 기록될 수 있다 | 과대 측정 가능성(B, 미재현) |
| `CLUSTER_ANIMATE` | 켜면 중간값이 먼저 그려지므로 `t_frame`은 목표값이 아니라 첫 중간값이 그려진 시점이 된다 | README도 측정 시 끄기를 권장한다 |
| 시간 해상도 | `QElapsedTimer::elapsed()`는 ms 정수 → 1 ms 양자화, 0 ms 가능 | – |
| 시계 | 같은 프로세스 안의 단조 시계 1개. 시계 동기화 오차는 없다 | – |
| simulation 입력 | `CLUSTER_SIMULATE=1`: 20 ms(50 Hz) QTimer로 `acceptRpmSample(..., "simulation")` 호출. 시리얼 경로(`readSerialData`)는 거치지 않는다 | 50 Hz > 30 Hz이므로 샘플 일부가 반드시 덮어쓰인다 |

## 3. 측정 구간 밖에 있는 지연 (참고 계산, 측정 아님)

| 구간 | 비고 |
|---|---|
| UART 전송 | 9600 baud, 8N1 = 문자당 10 bit → 약 1.04 ms/문자. `"2500\n"`(5문자)이면 약 5.2 ms. **계산값이며 측정하지 않았다** |
| Arduino 처리, USB-Serial 어댑터 버퍼링, OS 드라이버 | 미측정 |
| Qt event loop가 `readyRead`를 처리할 때까지의 대기 | 미측정 |
| 화면 반영(compositor, vsync 최대 1 프레임 등) | 미측정 |

## 4. 결론: 이 수치가 의미하는 것

연구 당시 측정한 E2E 지연은 **"Qt 애플리케이션이 RPM 샘플을 파싱해 받아들인 시점부터, 그 값이 rpmLabel의 paintEvent에서 그려진 시점까지"** 다. 이 구간은 다음을 포함한다.

- UI throttle 대기(0~33 ms)
- Qt의 paint 스케줄링

센서에서 실제 화면 픽셀까지의 지연이 아니다. README 서두의 "센서 데이터가 화면에 표시되기까지의 E2E" 표현은 실제 측정 구간보다 넓다. 이 문서의 정의가 코드와 일치한다.

33.3 ms는 이 구간에 대한 **soft budget(UI 30 Hz 목표)** 이며, hard real-time deadline이 아니다. 구조상 pending 대기만으로 최대 33 ms가 소비될 수 있다. 그래서 이 budget과의 비교는 throttle 주기와 paint 지연을 합친 값이 한 프레임 주기 안에 들어오는지를 보는 것이다.
