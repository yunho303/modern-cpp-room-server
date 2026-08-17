# 0005. A Room has one state-owning Worker

## Status

Accepted for the first Room implementation

## Context

여러 coroutine Session은 동시에 join, move와 leave를 요청할 수 있습니다. I/O thread에서 Room을 직접 수정하면
네트워크 처리와 게임 로직의 실행 시간이 결합되고, I/O thread를 늘릴 때 Room 상태 전체에 동기화가 필요해집니다.
또한 `PacketView`는 Session 수신 버퍼를 참조하므로 비동기 queue에 그대로 보관할 수 없습니다.

## Decision

Session은 packet framing과 gameplay payload를 검증한 뒤 필요한 값만 소유하는 `RoomCommand`를 생성합니다.
여러 Session은 mutex 기반 `CloseableQueue`에 명령을 submit하고, 하나의 `std::jthread` Room Worker만 FIFO로
명령을 꺼내 `Room::apply`를 호출합니다.

```text
TCP bytes -> PacketView -> owned RoomCommand -> CloseableQueue -> Room Worker -> Room
```

Room은 thread-safe 타입이 아니며 의도적으로 mutex를 갖지 않습니다. 실행 중인 Room 조회도 직접 접근하지 않고
추후 query command 또는 immutable snapshot 경로를 사용합니다. 종료 시 queue를 먼저 닫아 새 명령을 거절하고,
이미 접수한 명령을 처리한 뒤 Worker를 join합니다.

## Consequences

- Queue 경계에서만 짧게 lock을 사용하고 게임 상태 변경에는 lock이 필요하지 않습니다.
- 한 Room의 명령 순서는 결정적이지만 긴 Room 작업은 뒤의 명령을 지연시키므로 처리 시간을 측정해야 합니다.
- 여러 Room은 서로 다른 Worker 또는 worker shard에 배치해 병렬화할 수 있습니다.
- Queue는 아직 개수와 byte 한도가 없으므로 Session 연결 다음 단계에서 backpressure 정책이 필요합니다.
- Room 결과를 Session에 전달할 outbound 경로는 별도 설계가 필요합니다.
