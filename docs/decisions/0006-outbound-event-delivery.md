# 0006. Room Events use shared immutable outbound packets

## Status

Accepted for the first broadcast implementation

## Context

Room 상태 변경 결과는 여러 Session에 같은 내용으로 전달됩니다. Session마다 packet을 다시 직렬화하면 동일한
allocation과 byte copy가 반복되고, 느린 client의 socket write를 Room Worker에서 기다리면 뒤의 게임 명령이
모두 지연됩니다. 반대로 제한 없는 송신 queue는 한 연결이 서버 메모리를 계속 점유하게 만듭니다.

## Decision

Room은 성공한 Command에 대해서만 `RoomEvent`를 반환합니다. Room Worker의 callback은 Event를 wire packet으로
한 번 encode하고 `shared_ptr<const vector<byte>>`로 만든 뒤 Session Registry에 전달합니다. Registry는 Room
참여자를 선택하고 각 Session의 executor에 `asio::post`합니다.

Session의 OutboundQueue는 executor 한 곳에서만 접근하므로 mutex를 갖지 않습니다. packet 수가 아니라 대기 중인
총 byte를 제한하며, 한도 초과 시 해당 Session을 종료하고 `LeaveCommand`를 제출합니다. 하나의 writer coroutine만
queue를 비우므로 같은 socket에 여러 write가 동시에 실행되지 않습니다.

```text
RoomEvent -> encode once -> SharedPacket -> Registry -> asio::post
          -> per-Session OutboundQueue -> one writer coroutine -> TCP
```

Room 참여 여부와 단순 TCP 연결 여부는 다르므로 Registry가 별도로 관리합니다. Join Event가 발생한 뒤 참여자로
추가하고, Leave Event는 참여자에서 제거한 뒤 남은 Session에 방송합니다.

## Consequences

- Broadcast 대상 수와 무관하게 payload 직렬화는 한 번만 수행합니다.
- 느린 Session 하나의 backlog는 다른 Session의 Queue와 socket write를 막지 않습니다.
- `shared_ptr` reference count 비용과 Event당 allocation은 Week 4 측정 대상입니다.
- Queue는 현재 모든 packet을 보존하며, 위치 갱신을 최신 값으로 합치는 정책은 측정 후 검토합니다.
- 여러 I/O thread를 사용할 때는 Session executor를 strand로 바꾸거나 동일한 직렬 실행 보장을 추가해야 합니다.
