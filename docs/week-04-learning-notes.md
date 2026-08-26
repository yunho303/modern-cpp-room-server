# Week 4 Learning Notes

## 측정에서 시작한 이유

처음에는 `SessionRegistry`의 lock, `shared_ptr` reference count, packet allocation처럼 의심할 곳이 많았습니다.
하지만 확인 없이 memory pool이나 lock-free queue를 넣으면 코드만 복잡해지고 실제 병목은 남을 수 있습니다. 그래서
먼저 같은 조건을 반복할 coroutine load client를 만들고, Room 크기에 따른 처리량과 end-to-end latency를 기록했습니다.

baseline에서 8~64 clients의 추정 전달 처리량이 50K~57K deliveries/s에 머문 것이 첫 단서였습니다. command 하나가
Room 인원만큼 송신되는 구조이므로, Room 크기가 커질수록 command 처리량은 감소하고 latency는 빠르게 증가했습니다.

## Profile을 보고 바꾼 판단

64-client CPU profile의 application hot path에는 `OutboundQueue::pop`, `push`, `async_write` 준비와
`SessionRegistry::publish`가 있었습니다. 이미 Room Event는 한 번만 encode하고 immutable packet을 모든 Session이
공유하고 있었으므로, payload copy보다 작은 packet마다 write operation을 만드는 횟수를 먼저 줄이는 편이 맞다고
판단했습니다.

## 코드에 사용한 현대 C++

- `std::array`: writer coroutine마다 최대 batch 저장 공간을 한 번 정하고 반복 allocation을 피합니다.
- `std::span`: Queue가 배열 타입과 크기에 종속되지 않고 caller가 제공한 연속 영역만 빌려 씁니다.
- `shared_ptr<const vector<byte>>`: 여러 Session이 같은 packet을 공유하면서 `co_await` 동안 payload 수명을 보장합니다.
- Coroutine: write가 끝날 때까지 지역 배열이 coroutine frame에 유지되므로 buffer sequence가 dangling 되지 않습니다.
- `std::expected`: Queue push 실패가 closed, invalid packet, size limit 중 무엇인지 호출자가 처리합니다.

`span` 자체는 소유권을 갖지 않습니다. 안전한 이유는 `SharedPacket` 배열과 `const_buffer` 배열이 모두
`write_queued_packets` coroutine의 지역 상태이고, coroutine frame이 `co_await async_write`가 끝날 때까지 유지되기
때문입니다. write 완료 후에만 `SharedPacket`을 reset합니다.

## 왜 무제한으로 묶지 않았는가

batch가 너무 크면 한 Session의 backlog가 I/O 실행 시간을 오래 차지하고, 비동기 write 중 붙잡는 메모리도
커집니다. 그래서 packet 64개와 64 KiB라는 두 경계를 함께 사용했습니다. 첫 packet 하나가 byte 경계를 넘는 경우에는
그 packet만 전송해 queue가 진행하지 못하는 상황을 막았습니다.

## 결과를 해석하는 방법

1 client는 묶을 backlog가 거의 없어 처리량이 1.03배로 비슷했습니다. 반면 64 clients에서는 9.15배가 됐습니다.
효과가 fan-out과 함께 커졌다는 점이 중요합니다. 모든 코드를 막연히 빠르게 만든 것이 아니라, profile에서 찾은
packet별 송신 비용을 batch가 필요한 구간에서 줄였다는 뜻입니다.

한 번의 최대 latency나 localhost 결과를 production 수치로 과장해서는 안 됩니다. 이번 결과가 증명하는 범위는
동일한 machine, 동일한 protocol, 동일한 sample 수에서 송신 배치 구조가 기존 packet별 write보다 효율적이라는
것입니다.
