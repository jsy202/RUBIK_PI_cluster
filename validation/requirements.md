# Requirements — RUBIK Pi Qt Digital Cluster (Post-project Validation)

> 연구 종료 후(2026-10) 정리한 요구사항이다. 연구 당시의 결과 수치(포스터/논문의 평균 지연, jitter)는 **이 저장소에 기록되어 있지 않다.** 그래서 이 문서와 새 측정은 그 수치를 인용하거나 재해석하지 않는다.
>
> 출처: Existing = README/코드에 명시됨, Derived = 기존 설계에서 도출됨, Proposed = 이번에 새로 제안함. 증거 등급: A = 실행, B = 정적 분석, C = 제안.

## 입력 / 파싱

| ID | 요구사항 | 출처 | 검증 |
|---|---|---|---|
| REQ-IN-001 | 시리얼 입력은 줄(`\n`)마다 정수 1개(10진수)다. 공백과 `\r`은 허용한다 | Existing (README 실험 절차, readSerialData) | A (QtTest) |
| REQ-IN-002 | 정수가 아니거나 int 범위를 벗어나거나 빈 줄이면, 그 줄만 버리고 다음 줄을 계속 처리한다 | Existing | A |
| REQ-IN-003 | 한 줄이 여러 번의 read에 나뉘어 도착해도 하나의 값으로 조립한다 | Existing (버퍼 유지) | A |
| REQ-IN-004 | 64바이트를 넘는 줄은 버린다. 버퍼가 8 KiB를 넘으면 가장 오래된 바이트부터 버린다 | Existing ("너무 긴 라인 무시") | A |
| REQ-IN-005 | 음수 RPM은 0으로 보정한다. 상한은 없다 | Existing (`correctRpmValue`) | A |
| REQ-IN-006 | 파서를 추출하기 전과 후의 동작이 같다 | Derived (리팩터링 안전성) | A (차등 테스트) |

## UI / 측정

| ID | 요구사항 | 출처 | 검증 |
|---|---|---|---|
| REQ-UI-001 | UI는 약 30 Hz(33 ms 주기 timer)로 최신 샘플 1개만 반영한다 | Existing (README "30Hz UI 갱신 제한") | B (코드), A (seq 공백으로 관측) |
| REQ-PERF-001 | 측정 구간(`t_in`→`t_frame`, `measurement_boundary.md`)의 E2E 지연은 p95 기준 **33.3 ms soft budget** 이하를 목표로 한다. hard real-time deadline이 아니다 | Derived (30 Hz 목표) | A (이 PC의 Docker offscreen 측정만) |
| REQ-PERF-002 | 지연 보고서는 지연 통계와 함께 **로그되지 않은 샘플 수**(덮어쓰기, 같은 값)를 보고한다 | Proposed → 분석 스크립트에 구현 | A |
| REQ-PERF-003 | 측정 결과에는 환경(하드웨어, 플랫폼, 입력 패턴)을 함께 기록하고, 서로 다른 환경의 결과를 같은 조건이라고 주장하지 않는다 | Proposed | 문서 |
| REQ-MEAS-001 | 하드웨어 없이 같은 입력 시퀀스로 반복 측정할 수 있다(`CLUSTER_SIMULATE`, `CLUSTER_REPLAY_FILE`) | Existing (simulate) + Proposed (replay) | A |

## Proposed / 미구현

| ID | 내용 | 상태 |
|---|---|---|
| REQ-PERF-P01 | 덮어쓴 샘플 수를 앱이 CSV에 직접 기록 | C — CSV 형식(연구 산출물)을 바꾸게 되므로 하지 않음. seq 공백으로 계산 |
| REQ-PERF-P02 | 물리 화면 표시까지의 지연 측정(포토다이오드/카메라) | C — 하드웨어 필요 |
| REQ-PERF-P03 | 시리얼 바이트 도착 시점(`readyRead` 진입 시각) 별도 기록 | C |
