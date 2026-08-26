# 0007. Batch queued packets into one composed write

## Status

Accepted after the first Room broadcast baseline

## Context

최초 Release benchmark에서 client 수가 8명에서 64명으로 늘어도 추정 전달 처리량은 약 50K~57K deliveries/s에
머물렀습니다. 64명에서는 command 처리량이 781.844/s까지 떨어지고 p50 latency가 76.503 ms까지 증가했습니다.

64-client CPU profile에서 application hot path는 `OutboundQueue::pop`, `push`, `async_write` 준비와
`SessionRegistry::publish`였습니다. 같은 immutable packet을 공유해 직렬화와 payload 복사는 이미 한 번만
수행했지만, 각 Session writer는 대기 packet을 하나씩 꺼내 composed write를 매번 새로 시작했습니다.

## Decision

`OutboundQueue::pop_batch`는 caller가 제공한 `span<SharedPacket>`에 FIFO 순서로 packet을 이동합니다. Session
writer는 coroutine frame에 다음 두 고정 배열을 보관합니다.

- `array<SharedPacket, 64>`: 비동기 write가 끝날 때까지 각 payload의 소유권을 유지합니다.
- `array<asio::const_buffer, 64>`: 서로 떨어진 payload 메모리를 하나의 buffer sequence로 표현합니다.

writer는 최대 64 packet, 최대 64 KiB를 한 `async_write`에 전달합니다. 이는 payload를 하나의 연속 buffer로
복사하는 방식이 아닙니다. Asio의 `async_write`는 전체 buffer sequence가 처리될 때까지 필요한 socket write를
이어가는 composed operation이므로, partial write에서도 순서와 완전 전송 조건을 유지합니다.

두 개의 상한은 batch 하나가 I/O executor를 오래 점유하거나 과도한 메모리를 붙잡지 않게 합니다. 첫 packet이
64 KiB보다 크면 그 packet 하나는 꺼내어 queue가 영원히 멈추지 않게 합니다. Session의 전체 대기 한도 256 KiB와
한 writer만 socket을 사용하는 규칙은 유지합니다.

## Alternatives considered

- **연속 buffer로 합치기:** syscall 수는 줄일 수 있지만 매 batch allocation과 payload copy가 추가됩니다.
- **무제한 gather write:** 순간 처리량은 좋아질 수 있지만 큰 backlog가 한 Session의 실행 시간을 독점할 수 있습니다.
- **memory pool 또는 `std::pmr`:** profile에서 먼저 확인된 비용은 packet별 write 경로였으므로 적용하지 않았습니다.
- **여러 writer coroutine:** 같은 socket write 순서와 queue 상태를 다시 동기화해야 하므로 현재 ownership 모델과 맞지 않습니다.

## Consequences

- packet payload를 다시 복사하지 않고 packet별 write 준비와 완료 횟수를 줄입니다.
- `span`은 소유하지 않으므로 실제 packet과 buffer 배열은 coroutine frame에서 `co_await` 종료까지 유지해야 합니다.
- Queue의 `pending_bytes`는 기존과 같이 현재 write 중인 batch를 제외합니다. batch는 64 KiB로 제한되어 있으며,
  단일 대형 packet의 최악 조건은 기존 한 packet write와 같습니다.
- 작은 Room에서는 batch에 묶일 backlog가 적어 개선 폭이 작고, fan-out이 큰 Room일수록 효과가 커집니다.
- 같은 조건의 5회 중앙값에서 64-client command 처리량은 9.15배, p50은 88.5%, p99는 91.0% 개선됐습니다.
