# Week 2 Learning Notes

## 실행 모델

`io_context::run()`은 호출한 main thread에서 준비된 handler와 coroutine을 실행합니다. 진행 중인 I/O만 있고 실행
가능한 작업이 없으면 운영체제의 완료 통지를 기다리며 thread가 대기합니다. 현재는 `run()`을 한 thread만 호출하므로
여러 Session이 동시에 진행 중이어도 C++ 코드는 한 순간에 하나만 실행됩니다.

`co_spawn`은 `run_server` 또는 `run_session` coroutine을 실행 환경에 독립 작업으로 등록합니다. executor는 thread의
주소가 아니라 같은 `io_context`에 작업을 제출하는 손잡이입니다. Server가 Session을 `co_await`하지 않고
`co_spawn`하므로 한 Session의 종료를 기다리지 않고 다음 접속을 받을 수 있습니다.

## 중단과 재개

`co_await` 대상이 아직 준비되지 않았다면 현재 coroutine의 실행 위치와 지역변수를 frame에 보관하고 실행권을
반환합니다. 접속, 수신 또는 송신 완료가 통지되면 coroutine이 실행 가능한 작업이 되고, `io_context`가 차례에 맞춰
중단된 다음 줄부터 재개합니다. `this_coro::executor`처럼 즉시 준비되는 awaitable은 정보를 반환할 뿐 중단하지
않습니다.

Session의 `read_chunk`와 ping `response`는 coroutine frame에 있으므로 각각 read와 write가 완료될 때까지 살아
있습니다. `asio::async_write`는 일부 송신이 발생하면 내부적으로 `async_write_some`을 반복하고, 전체 buffer가
전송된 후 Session coroutine을 재개합니다.

## 오류 처리

`use_awaitable`만 사용하면 Asio의 I/O 오류가 `system_error` 예외로 전달됩니다. `redirect_error`는 그 I/O 오류를
지역 `error_code`로 돌려 정상적인 연결 종료와 실제 실패를 코드에서 구분하게 합니다. 메모리 할당 실패처럼 I/O
`error_code`가 아닌 예상하지 못한 예외는 `co_spawn` 완료 handler의 `exception_ptr`에서 관찰합니다.

## 리뷰에서 발견한 다음 설계 경계

현재 Session은 게임 상태가 없어 coroutine이 끝나면 socket과 함께 파괴해도 됩니다. 하지만 Room에 참여한 뒤에는
연결 종료가 Player 상태의 즉시 파괴를 의미하지 않습니다. Network Session은 종료 사유를 가진 command를 보내고,
Room이 Player 제거와 상태 정리를 소유해야 합니다. 이 판단은 Week 3의 SessionId, Room command와 단일 상태 소유권
설계에 반영합니다.

## 검증한 동작

- 실제 loopback TCP 연결에서 ping 요청과 응답
- 하나의 ping을 두 client write로 나누어 전송
- 두 ping을 하나의 client write로 결합해 전송
- 알 수 없는 packet type을 받은 Session의 연결 종료
- client가 연결을 닫은 뒤 Session coroutine과 socket 정리

TCP가 두 write를 반드시 두 read로 전달하는 것은 아니므로, byte가 실제로 분할된 입력은 ReceiveBuffer 단위
테스트에서 별도로 검증합니다.
