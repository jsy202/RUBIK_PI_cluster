# Limitations — RUBIK Pi Qt Digital Cluster Validation

| 항목 | 상태 |
|---|---|
| RUBIK Pi 3 하드웨어, Arduino, 센서 | **없음.** 이번 측정은 개발 PC(x86_64)의 Docker(Ubuntu 20.04, Qt 5.12.8) + Qt **offscreen** 플랫폼에서 실행했다 |
| 실제 디스플레이 | 없음. offscreen 플랫폼은 화면에 내보내지 않으므로, 실제 윈도 시스템에서의 paint 비용과 스케줄링은 반영되지 않는다 |
| 시리얼 경로(`readSerialData`) 성능 | 측정하지 않았다. 측정에는 `CLUSTER_SIMULATE`와 `CLUSTER_REPLAY_FILE`만 썼고, 둘 다 `acceptRpmSample()`을 직접 호출한다. 파서 자체는 기능 테스트(QtTest)만 했다 |
| 연구 당시 결과와 비교 | **불가.** 연구 당시 수치가 저장소에 없고, 하드웨어, 플랫폼, 입력이 모두 다르다. 이번 수치를 RUBIK Pi 결과로 해석하면 안 된다 |
| 물리 표시 지연 | 정의상 측정 구간 밖이다(`measurement_boundary.md`) |
| `CLUSTER_ANIMATE=1` | 측정하지 않았다 |
| 다른 원인의 repaint로 인한 과대 측정 가능성 | 당시에는 정적 분석으로만 확인했다. 이후 host 테스트로 재현했다. Known Measurement Limitation DEF-SW-03이며 [software_verification/limitations.md](software_verification/limitations.md)를 참조한다 |
| GitHub Actions | 실행 기록 없음(push 안 함). 같은 단계(Docker 빌드, QtTest, pytest)는 로컬에서 실행했다 |
| 반복 횟수 | 시나리오당 3회. 통계적 유의성을 주장하지 않는다 |

## 이번에 추가하거나 변경한 production 코드

1. `rpmparser.h` 추출: 동작은 같다. 차등 테스트로 확인했다.
2. `CLUSTER_REPLAY_FILE` 입력 경로: 환경변수가 없으면 비활성. 기존 serial/simulation 경로와 CSV 형식은 바꾸지 않았다.
