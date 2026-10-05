# 2026-08-27 Windows Outbound Batching Result

이 문서는 `TCP_NODELAY`를 명시적으로 설정·확인하지 않았던 최초 측정 기록입니다. 9.15배 개선을
application write 비용 감소만의 효과로 단정하지 않습니다. Nagle을 양쪽에서 비활성화한 후속 비교는
[2026-09-22 TCP_NODELAY 통제 재측정](2026-09-22-windows-nodelay-comparison.md)에 별도로 기록합니다.

## Hypothesis

최초 baseline은 8~64 clients 구간에서 추정 전달 처리량이 약 50K~57K deliveries/s에 머물렀습니다. CPU profile에서
`OutboundQueue::pop`, `push`, `async_write` 준비와 `SessionRegistry::publish`가 application hot path로 확인됐습니다.
작은 Room Event packet을 하나씩 꺼내 별도의 composed write를 시작하는 비용이 fan-out과 함께 커진다고 판단했습니다.

## Change

Session writer가 대기 packet을 최대 64개, 최대 64 KiB까지 FIFO batch로 꺼내 하나의 Asio buffer sequence로
전달하도록 변경했습니다. `SharedPacket` 배열이 payload 수명을 유지하므로 연속 buffer를 새로 만들거나 payload를
복사하지 않습니다. protocol, Event 수, client flow control과 benchmark 조건은 변경하지 않았습니다.

## Method

- Environment, topology and compiler: baseline과 동일
- Build: Release
- Client matrix: 1, 8, 16, 32, 64
- Repetitions: 5 per client count
- Warm-up: 100 Move commands per client
- Measurement: 2,000 Move commands per client
- Flow control: one in-flight Move per client
- Aggregation: nearest-rank latency percentiles and median of five runs

## Before and after

| Clients | Commands/s before | Commands/s after | Throughput | p50 before | p50 after | p99 before | p99 after |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 15,431.944 | 15,882.672 | 1.03x | 0.059 ms | 0.057 ms | 0.157 ms | 0.196 ms |
| 8 | 6,543.551 | 14,282.785 | 2.18x | 1.173 ms | 0.511 ms | 2.028 ms | 1.102 ms |
| 16 | 3,575.642 | 13,569.903 | 3.80x | 4.372 ms | 1.071 ms | 6.964 ms | 2.209 ms |
| 32 | 1,701.132 | 11,269.115 | 6.62x | 17.850 ms | 2.703 ms | 37.541 ms | 4.552 ms |
| 64 | 781.844 | 7,150.599 | 9.15x | 76.503 ms | 8.795 ms | 155.117 ms | 13.943 ms |

64 clients에서 추정 전달 처리량은 50,038/s에서 457,638/s로 증가했습니다. p50은 88.5%, p99는 91.0%
감소했습니다. 개선 폭이 Room 크기와 함께 증가하고 1 client에서는 거의 차이가 없다는 점은, 고정 연산을 빠르게
만든 것이 아니라 대기 packet을 실제로 묶을 수 있는 fan-out 구간의 write 횟수를 줄였다는 가설과 일치합니다.

후속 64-client profile에서도 같은 12,800개 측정 sample을 처리했으며 batch queue extraction과 buffer-sequence
write 경로가 확인됐습니다. profile trace는 수집 overhead와 machine-specific data를 포함하므로 저장소에는 넣지 않고,
최종 판단은 동일한 Release benchmark 25회의 전후 결과를 기준으로 했습니다.

## Honest limits

- 1-client median p99는 0.157 ms에서 0.196 ms로 증가했습니다. 절대 차이는 작고 p50과 처리량은 비슷하므로 batch
  효과가 없는 저부하 구간의 scheduler 변동으로 봅니다.
- 8-client 최적화 결과 중 한 run의 단일 최대 latency가 60.273 ms였습니다. p99 중앙값은 개선됐지만 최대 한 건은
  안정적인 대표값으로 사용하지 않습니다.
- `Estimated deliveries/s`는 `commands/s * Room member count`이며 실제 socket completion counter가 아닙니다.
- server와 load client가 같은 6-core localhost machine을 사용하므로 양쪽 CPU와 loopback network stack이 경쟁합니다.
- server와 load client 모두 `io_context.run()` thread 하나를 사용합니다. 이 결과는 production capacity가 아니라
  동일 환경에서 한 구조 변경의 효과를 비교한 값입니다.

최적화 후 raw runs는 [2026-08-27-windows-outbound-batching-raw.csv](2026-08-27-windows-outbound-batching-raw.csv),
중앙값은 [2026-08-27-windows-outbound-batching-summary.csv](2026-08-27-windows-outbound-batching-summary.csv)에 있습니다.
