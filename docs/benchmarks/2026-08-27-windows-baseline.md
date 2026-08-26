# 2026-08-27 Windows Room Broadcast Baseline

후속 profiling과 송신 배치 결과는 [2026-08-27-windows-outbound-batching.md](2026-08-27-windows-outbound-batching.md)에 기록했습니다.

## Environment

- CPU: AMD Ryzen 5 3500X, 6 cores / 6 logical processors
- OS: Microsoft Windows NT 10.0.26200.0
- Compiler: MSVC 19.38.33133.0, x64
- Generator: Visual Studio 17 2022
- CMake: 3.30.2
- Build: Release
- Topology: server and load client are separate processes on the same localhost machine
- Execution: one `io_context.run()` thread in the server and one in the load client

## Scenario

- Client matrix: 1, 8, 16, 32, 64
- Repetitions: 5 per client count
- Warm-up: 100 Move commands per client
- Measurement: 2,000 Move commands per client
- Flow control: one in-flight Move per client
- Completion: the sending client receives its matching `PlayerMovedEvent`
- Fan-out: every measured Move is broadcast to all current Room members
- Aggregation: nearest-rank latency percentiles and median of five runs

The Release server was started once by `scripts/run-benchmark.ps1`. Every client group entered an empty Room because the preceding group completed its ordered leave sequence before the next run.

## Median Results

| Clients | Samples/run | Elapsed | Commands/s | Estimated deliveries/s | p50 | p95 | p99 | Largest max |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2,000 | 129.601 ms | 15,431.944 | 15,431.944 | 0.059 ms | 0.098 ms | 0.157 ms | 11.453 ms |
| 8 | 16,000 | 2,445.156 ms | 6,543.551 | 52,348.407 | 1.173 ms | 1.620 ms | 2.028 ms | 26.300 ms |
| 16 | 32,000 | 8,949.441 ms | 3,575.642 | 57,210.275 | 4.372 ms | 5.414 ms | 6.964 ms | 28.474 ms |
| 32 | 64,000 | 37,622.005 ms | 1,701.132 | 54,436.227 | 17.850 ms | 22.363 ms | 37.541 ms | 146.122 ms |
| 64 | 128,000 | 163,715.453 ms | 781.844 | 50,038.038 | 76.503 ms | 109.892 ms | 155.117 ms | 720.735 ms |

The raw runs are in [2026-08-27-windows-baseline-raw.csv](2026-08-27-windows-baseline-raw.csv), and the generated medians are in [2026-08-27-windows-baseline-summary.csv](2026-08-27-windows-baseline-summary.csv).

## Interpretation

Command throughput falls as Room size grows because each command creates N outbound deliveries. From 8 through 64 clients, estimated delivery throughput stays around 50K to 57K deliveries/s instead of growing with N. The 16-client condition has the highest median estimated delivery rate, while the 64-client condition falls to about 50K and has substantially higher tail latency.

This result identifies a saturation region but does not identify its cause. The next step is to profile the 16, 32, and 64-client conditions and compare CPU time and allocation counts in `SessionRegistry::publish`, recipient collection, `shared_ptr` reference counting, `asio::post`, and per-Session outbound queue operations.

## Limits

- `Estimated deliveries/s` is `commands/s * Room member count`; it is not a direct socket-completion counter.
- The load generator shares the same six-core machine with the server, so client parsing and scheduling compete for CPU time.
- The load client uses one I/O thread and may become part of the bottleneck at higher client counts.
- Localhost removes real network delay and packet loss.
- The largest single latency is reported separately; scheduler outliers should not be treated as typical latency.

These values are suitable as a same-machine optimization baseline. They are not a production capacity claim.
