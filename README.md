# Modern C++ Room Server

현대 C++의 소유권, 오류 처리, 동시성 기능을 사용해 만드는 권위형(authoritative) Room 서버입니다.
기능을 많이 나열하기보다 설계 판단을 기록하고, 부하 테스트와 프로파일링 결과로 최적화를 검증하는 것을
목표로 합니다.

## Current milestone

- [x] C++23 빌드 환경과 엄격한 컴파일러 경고
- [x] 길이 기반 패킷 프레이밍과 명시적 오류 모델
- [x] TCP read 결과를 누적하고 소비하는 수신 버퍼
- [x] Coroutine 기반 비동기 TCP Session과 ping 왕복
- [x] I/O 스레드와 Room Worker의 상태 소유권 분리
- [x] Room Event broadcast와 byte 제한 Session 송신 Queue
- [x] Coroutine 봇 클라이언트와 반복 가능한 성능 기준값 측정
- [x] 프로파일링 기반 송신 배치 최적화와 전후 비교
- [x] v0.2 첫 단계: post/Queue/in-flight 및 송신 batch 계측과 실제 연결 통계 검증

## Measured result

서버와 부하 클라이언트 모두 Nagle을 비활성화한 64-client Room broadcast 조건에서 packet별 write를
bounded gather write로 변경한 뒤, 5회 중앙값 기준 처리량은 825.428에서 6,326.567 commands/s로 **7.66배**
증가했고 p99 latency는 112.418 ms에서 19.354 ms로 감소했습니다. 조건과 한계, 원본 CSV는
[TCP_NODELAY 통제 재측정](docs/benchmarks/2026-09-22-windows-nodelay-comparison.md)에 기록했습니다.

TCP_NODELAY를 명시적으로 설정·확인하지 않았던 최초 9.15배 결과는
[기존 benchmark 기록](docs/benchmarks/2026-08-27-windows-outbound-batching.md)에 보존했습니다.

## Non-goals for v0.1

- 완성형 MMORPG 콘텐츠
- 그래픽 클라이언트
- DB, Redis, MSA 등 사용 이유가 아직 없는 인프라 추가
- 측정 없이 적용하는 lock-free 자료구조와 메모리 풀

## Build

Visual Studio 2022와 CMake 3.25 이상이 필요합니다. configure 과정에서 standalone Asio 1.36.0을 내려받습니다.

```powershell
cmake --preset vs2022
cmake --build --preset debug
ctest --preset debug
```

서버는 기본 TCP port 7777을 사용하며 실행 인자로 변경할 수 있습니다.

```powershell
.\out\build\vs2022\Debug\mcrs_server.exe 7777
```

서버의 두 번째 인자로 출력 간격(ms)을 주면 계측을 활성화합니다. 예를 들어 `mcrs_server.exe 7777 1000`은
1초마다 JSON을 출력합니다. 생략 또는 0이면 비활성화합니다. [지표 정의와 검증 방법](docs/server-metrics.md)을
참고합니다.

## Engineering principles

1. 게임 상태는 소유자가 명확한 실행 흐름에서만 변경합니다.
2. 객체 수명과 오류를 타입으로 표현하고 정상 흐름에서 숨기지 않습니다.
3. 최적화는 기준값 측정과 프로파일링 이후에 수행합니다.
4. 모든 기능은 실패, 취소, 종료 경로까지 테스트합니다.

## Documents

- [v0.2 operational plan and Week 4 code review](docs/v0.2-operational-plan.md)
- [Server metrics and validation](docs/server-metrics.md)
- [Architecture](docs/architecture.md)
- [Code walkthrough](docs/code-walkthrough.md)
- [C++ language level decision](docs/decisions/0001-cpp-language-level.md)
- [Packet framing decision](docs/decisions/0002-packet-framing.md)
- [Session receive buffer decision](docs/decisions/0003-session-receive-buffer.md)
- [Coroutine Session ownership decision](docs/decisions/0004-coroutine-session-ownership.md)
- [Single-owner Room decision](docs/decisions/0005-single-owner-room.md)
- [Outbound event delivery decision](docs/decisions/0006-outbound-event-delivery.md)
- [Batched outbound write decision](docs/decisions/0007-batched-outbound-write.md)
- [Room broadcast benchmark scenario](docs/benchmark-scenario.md)
- [Windows Room broadcast baseline](docs/benchmarks/2026-08-27-windows-baseline.md)
- [Windows outbound batching result](docs/benchmarks/2026-08-27-windows-outbound-batching.md)
- [Windows TCP_NODELAY-controlled comparison](docs/benchmarks/2026-09-22-windows-nodelay-comparison.md)
- [Week 1 review checklist](docs/week-01-review-checklist.md)
- [Week 1 learning notes](docs/week-01-learning-notes.md)
- [Week 2 learning notes](docs/week-02-learning-notes.md)
- [Week 4 learning notes](docs/week-04-learning-notes.md)
- [Four-week roadmap](docs/roadmap.md)
- [AI usage](AI_USAGE.md)
