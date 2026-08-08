# Four-Week Roadmap

평일에는 1시간 미만의 작은 단위로 읽기와 테스트를 진행하고, 주말 하루 3~4시간에 기능을 연결합니다.
목표는 기능 수를 늘리는 것이 아니라 C++23, 소유권, 동시성, 측정 기반 최적화를 설명 가능한 코드로 남기는 것입니다.

## Week 1 - Protocol foundation (완료)

- CMake와 Visual Studio 빌드 구조 구성
- 길이 기반 packet framing과 big-endian wire format 구현
- `span`, `expected`, `byte`, `byteswap`, concept 등 현대 C++ 기능 적용
- 정상, incomplete, invalid 입력 테스트와 Linux CI 구성
- 코드 리뷰 중 용도가 불명확한 `sequence`를 발견하고 제거

완료 기준: protocol 코드의 흐름과 사용한 C++ 기능을 설명하고, 설계 의문을 실제 변경과 테스트로 연결합니다.

## Week 2 - Coroutine TCP Session

평일의 작은 작업:

- Boost.Asio의 `io_context`, acceptor, socket, `awaitable` 실행 흐름 확인
- Session의 소유권과 종료 시점을 먼저 문서로 결정
- 수신 버퍼에 read 결과를 누적하고 `decode_one`을 반복 호출하는 loop 작성
- incomplete는 다음 read를 기다리고 invalid는 연결을 종료하는 테스트 작성

주말 통합 작업:

- coroutine 기반 TCP accept와 Session 연결
- ping packet을 받고 응답하는 최소 서버 완성
- 정상 종료, 상대 종료, 잘못된 packet, 여러 packet 동시 수신 경로 검증

완료 기준: TCP가 패킷을 나누거나 합쳐 보내도 올바르게 처리하고, Session 객체가 언제 생성되고 파괴되는지 설명합니다.

## Week 3 - Single-owner Room

평일의 작은 작업:

- Session과 Room 사이 command 타입을 `std::variant`로 정의
- I/O 흐름이 게임 상태를 직접 수정하지 않는 ownership 규칙 작성
- `std::jthread`와 `std::stop_token`을 이용한 Room worker 종료 흐름 구현
- mutex 기반 queue의 대기, 깨우기, 종료 조건 테스트

주말 통합 작업:

- 입장, 이동, 퇴장 command를 Room worker 한 곳에서 처리
- 여러 Session에 immutable packet buffer를 공유하는 broadcast 구현
- 느린 client에 대한 outbound queue 한도와 backpressure 정책 추가

완료 기준: 공유 게임 상태에 별도 lock이 필요 없는 이유와 queue, Session, outbound buffer의 소유권을 설명합니다.

## Week 4 - Measurement and optimization

평일의 작은 작업:

- 봇 client 시나리오와 측정 항목을 먼저 정의
- 처리량, p50/p95/p99 latency, allocation 수의 baseline 기록
- profiler로 실제 병목을 확인하고 개선 후보 한 가지 선택

주말 통합 작업:

- 측정 결과에 따라 buffer 재사용, shared immutable buffer 또는 `std::pmr` 중 한 가지 적용
- 같은 조건에서 최적화 전후 결과 비교
- architecture, decision record, README와 실행 방법 정리
- Release 빌드, 테스트, CI, 반복 가능한 benchmark 명령 최종 검증

완료 기준: 최신 C++ 기능을 사용했다는 나열이 아니라, 어떤 문제를 해결했고 성능이 얼마나 달라졌는지를 수치로 설명합니다.

## v0.1 결과물

- coroutine TCP Room server
- protocol, Session, Room 단위 테스트
- 재현 가능한 bot 부하 테스트
- profiler 근거가 있는 최적화 전후 비교
- 설계 결정과 AI 사용 범위를 포함한 문서
