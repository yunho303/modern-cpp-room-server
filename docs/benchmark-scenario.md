# Room Broadcast Benchmark Scenario

## 목적

Room broadcast 경로를 같은 조건에서 반복 측정하여 최적화 전후를 비교합니다. 이 벤치마크는 순수한 Room 처리 시간만이 아니라 client 송신부터 자기 `PlayerMovedEvent` 수신까지의 end-to-end latency를 측정합니다.

## 고정 시나리오

1. Release server를 localhost에서 실행합니다.
2. N개의 bot client가 접속하고 모두 Room에 입장합니다.
3. 입장 Event를 모두 소비한 뒤 시작 barrier에서 대기합니다.
4. 각 client는 자신의 이전 `PlayerMovedEvent`를 받은 뒤 다음 Move를 보내며, client당 하나의 요청만 진행 중인 상태를 유지합니다.
5. warm-up 구간은 통계에서 제외하고, 측정 구간의 Move만 기록합니다.
6. 모든 client가 목표 횟수를 완료하면 결과를 출력하고 정상 종료합니다.

기본값은 client 16개, client당 warm-up 20회와 측정 200회입니다. 모든 Move는 현재 Room 구성원 전체에 전파되므로 N개의 client가 모두 송신하면 전체 전달량은 대략 N x N으로 증가합니다.

## 측정 항목

- 완료한 Move command 수와 초당 처리량
- client가 Move를 보낸 시점부터 동일한 session의 `PlayerMovedEvent`를 받은 시점까지의 p50, p95, p99 latency
- 최소 및 최대 latency
- 연결 실패, protocol 오류, 예상하지 못한 연결 종료 수
- 이후 instrumentation으로 추가할 Event encode 및 outbound allocation 수

percentile은 nearest-rank 방식을 사용합니다. 예를 들어 정렬된 표본 100개의 p95는 95번째 값입니다. 통계 계산을 위한 정렬과 할당은 측정 구간이 끝난 후에만 수행합니다.

## 비교 규칙

- 동일한 Release binary와 localhost 환경에서 실행합니다.
- 각 조건을 5회 실행하고 가운데 결과를 대표값으로 사용합니다.
- 실행 중 debugger와 다른 고부하 프로그램을 사용하지 않습니다.
- 최적화 전후에 client 수, warm-up, 측정 횟수와 패킷 형식을 변경하지 않습니다.
- 평균값만으로 결론 내리지 않고 처리량과 p95/p99 변화도 함께 기록합니다.

첫 baseline은 구조가 정확하게 동작하는지 확인하기 위한 값입니다. profiler에서 병목이 확인되기 전에는 buffer pool, `std::pmr`, lock-free 구조를 먼저 적용하지 않습니다.
