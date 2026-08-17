# 0004. Coroutine frame owns each Session

## Status

Superseded for outbound Event delivery by decision 0006; retained as the Week 2 baseline

## Context

비동기 socket 작업이 대기하는 동안에도 socket, 수신 버퍼와 작업 중인 응답 버퍼가 살아 있어야 합니다.
callback 기반 코드에서는 이를 위해 Session을 `shared_ptr`로 관리하는 방식이 흔하지만, 현재 단계에는 여러 작업이
동시에 Session을 공유해야 할 요구사항이 없습니다.

## Decision

accept한 socket을 값으로 `run_session` coroutine에 이동합니다. coroutine 내부에서 만든 Session은 `run`이 끝날
때까지 coroutine frame에 존재합니다. `co_await`로 read나 write가 중단되어도 frame이 지역 변수와 Session의 수명을
유지하므로 현재 구조에는 `shared_ptr`가 필요하지 않습니다.

한 Session은 다음 작업을 순서대로 수행합니다.

1. 최대 4 KiB를 비동기로 읽습니다.
2. `ReceiveBuffer`에 추가하고 한도 초과를 검사합니다.
3. 완성된 packet을 모두 decode합니다.
4. ping은 payload를 소유하는 응답 buffer로 encode한 뒤 수신 byte를 소비합니다.
5. 응답 write를 기다린 후 다음 packet 또는 read로 진행합니다.

Asio 작업 오류는 `redirect_error`로 `error_code`에 받아 정상 종료, protocol 위반과 I/O 실패를 명시적으로
분기합니다. allocation이나 예상하지 못한 예외는 `co_spawn` completion handler에서 관찰합니다.

TCP 경계와 오류 정책은 실제 loopback socket을 사용해 검증합니다. ping 왕복, 하나의 packet을 두 write로 보낸
경우, 여러 packet을 한 write로 보낸 경우와 알 수 없는 packet type을 받은 연결의 종료를 확인합니다. TCP가 write와
read 경계를 보존하지 않는다는 점을 고려해 결정적인 byte 분할 검증은 ReceiveBuffer 단위 테스트에서 수행합니다.

## Buffer bound

Session의 최대 수신 누적량은 `최대 packet 크기 + read chunk 크기`입니다. 최대 packet의 마지막 일부를 기다리는
상태에서 다음 4 KiB read가 완료될 수 있기 때문입니다. read가 끝날 때마다 가능한 packet을 모두 소비하므로 이보다
더 큰 누적은 정상 흐름이 아닙니다.

## Consequences

- 현재 Session의 read와 write는 한 coroutine에서 직렬 실행되므로 별도 lock이 필요하지 않습니다.
- 느린 client에 응답을 쓰는 동안 다음 read를 하지 않으므로 메모리는 제한되지만 처리량은 이후 측정이 필요합니다.
- Room broadcast처럼 read와 write 요청이 독립적으로 생기면 outbound queue와 별도 writer coroutine을 추가해야 합니다.
- 여러 I/O thread를 사용할 때는 Session 작업을 같은 executor 또는 strand에 묶는 정책을 다시 결정해야 합니다.

## Game-state lifecycle

Session은 서버가 발급한 SessionId와 Room 가입 여부를 소유합니다. socket 종료, protocol 오류와 명시적 퇴장은
각각 종료 사유를 가진 `LeaveCommand`로 변환하고, Room은 자신의 실행 흐름에서 Player를 제거합니다. Session
소멸자는 게임 로직을 실행하지 않고 네트워크 자원 정리만 담당합니다.

## Evolution

Room Event가 read coroutine과 독립적으로 Session에 도착하면서 coroutine frame 하나만으로는 Session 수명을
보장할 수 없게 되었습니다. decision 0006부터 Session Registry는 weak pointer를 보관하고, 실행 중인 reader,
writer와 `asio::post` handler가 필요한 동안만 `shared_ptr`로 Session 수명을 연장합니다.
