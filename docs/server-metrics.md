# Server metrics: 전달 경로와 송신 batch 계측

## 구현 범위

v0.2 보강의 첫 단계입니다. 서버의 실제 command 처리, 전달 시도, composed write와 Session별 대기량을 관찰합니다.
**이 계측 자체가 post 이전의 과부하 제한이나 멀티스레드 Session 안전성을 구현한 것은 아닙니다.**
느린 client, admission 정책, graceful shutdown, Session strand는 [후속 계획](v0.2-operational-plan.md)입니다.

## 실행

```powershell
cmake --build --preset release
ctest --preset release
.\out\build\vs2022\Release\mcrs_server.exe 7777 1000
```

두 번째 서버 인자는 JSON 출력 간격(ms, 0~60,000)입니다. 생략하거나 0이면 계측을 비활성화합니다.
위 명령은 1초마다 stdout에 JSON 한 줄을 출력합니다. 기존 `mcrs_server.exe 7777` 실행도 지원합니다.
부하 클라이언트는 별도 terminal에서 기존 방식으로 실행합니다.

```powershell
.\out\build\vs2022\Release\mcrs_load_client.exe 127.0.0.1 7777 8 20 2000
```

시작 안내 줄 이후 `type=server_metrics`인 JSON을 수집합니다. `uptime_ms`는 계측 객체 생성 이후 경과 시간이고
`scope=process_lifetime`은 누적 범위입니다. warm-up·입장·퇴장과 barrier Move도 포함됩니다. 기존 benchmark의
측정 구간만을 나타내는 통계로 해석하지 않습니다. 계측 모드와 출력 간격을 성능 결과에 함께 기록해야 합니다.

## 프로세스 누적 counter

| 필드 | 의미 |
|---|---|
| room_commands_processed / room_commands_rejected | Room::apply 실행 수 / 그중 거절 수 |
| event_delivery_failures | Room Event callback 예외 수; 기존 RoomWorker 종료 summary와 같은 정의 |
| broadcast_delivery_attempts | 실제 recipient->deliver 호출 시도 수 |
| write_batches_started | writer가 async_write 시작 경로에 진입한 batch 수 |
| write_batches_completed | 완료 결과를 받은 batch 수; 실패한 완료도 포함 |
| write_batches_failed | completed 중 오류를 보고한 수; 정상적인 취소 완료도 오류로 집계 |
| write_batches_abandoned | 시작 경로에서 예외가 나거나 coroutine frame이 폐기되어 완료 결과를 기록하지 못한 수 |
| packets_batched / max_batch_packets | batch에 넣은 packet 개수의 합 / 한 batch의 최대 개수 |
| bytes_requested / bytes_transferred | batch 전체 논리적 byte 합 / Asio 완료 결과가 보고한 byte 합 |
| post_handlers_abandoned | handler 실행 전에 예약이 실패하거나 handler가 폐기된 수 |
| outbound_overflow_closes | Session 최초 종료 사유가 outbound_overflow였던 수 |
| registered_sessions | SessionRegistry에 등록된 항목 수 |
| active_rooms | 아직 stop을 완료하지 않은 RoomWorker의 Room 수; 현재 구조에서는 1개 |
| session_queue_peak_bytes | 전체 Session 중 가장 큰 개별 Queue high-water |

평균 batch packet 수는 `packets_batched / write_batches_started`입니다. 시작 수가 0이면 아직 평균을 구할 수
없습니다. 실패·폐기가 있을 때 이 평균은 성공적으로 수신된 packet만의 평균이 아닙니다.

`async_write`는 composed operation이므로 이 counter를 OS syscall 수 또는 TCP segment 수로 부르지 않습니다.
`bytes_transferred`에는 오류와 함께 보고된 partial byte도 포함하며, 상대 application의 수신/처리 확인을 의미하지
않습니다. 해당 계약은 [Asio 문서](https://think-async.com/Asio/asio-1.36.0/doc/asio/reference/async_write/overload1.html)를
따릅니다.

## Session별 gauge와 수명

`sessions`에는 수명이 유지되는 Session 계측 객체를 기록합니다. Registry 제거 뒤에도 writer나 post가 Session을
붙잡으면 이 배열에 잠시 남을 수 있으므로 registered_sessions와 의미가 다릅니다.

| 상태 | 현재 값 | 최대 값 |
|---|---|---|
| 예약됐으나 아직 실행하지 않은 post | posted_jobs / posted_bytes | posted_peak_bytes |
| OutboundQueue에서 대기 | queued_packets / queued_bytes | queue_peak_bytes |
| writer가 처리 중인 batch | inflight_packets / inflight_bytes | inflight_peak_bytes |

`PendingDelivery`는 post handler와 함께 이동합니다. handler 시작 시 post gauge를 해제하고, 실행되지 않은 채
executor가 파기되거나 예약 과정에서 예외가 발생해도 소멸자가 해제합니다. `io_context.stop()` 호출 자체는
예약된 handler를 폐기하지 않으므로 그 직후 gauge를 거짓으로 0으로 만들지 않습니다.

`WriteBatch`는 coroutine frame에 남아 완료·예외·frame 폐기 때 in-flight gauge를 해제합니다. 완료 결과를
받지 못한 폐기에 대해 추정 byte를 더하지 않습니다. Queue는 push/pop/close 및 Session 소멸 때 현재 값을 갱신합니다.

계측 객체는 Session을 소유하지 않습니다. 서버 쪽 목록은 weak_ptr를 사용합니다. 소멸한 계측 객체의 최종 상태는
`recent_closed_sessions`에 최근 128개까지만 보관하고, 밀려난 기록 수는 `closed_history_overwritten`에 표시합니다.
고빈도 접속에서는 보고 간격 사이에 개별 종료 이력이 유실될 수 있는 진단용 제한입니다.

모든 수치는 논리적 packet byte 기준입니다. shared packet의 중복 참조 합계, vector capacity, coroutine/handler
할당과 OS socket buffer를 포함한 RSS가 아닙니다. 세 gauge 사이의 이동 및 여러 counter 읽기는 하나의 원자적
snapshot이 아니므로 실행 중 합계의 일시적인 불일치를 과부하 한도 검사로 사용하지 않습니다. 정지 후에는
잔류 gauge가 0인지와 누적 counter의 정합성을 확인할 수 있습니다.

## 검증

`observability.server_metrics` 테스트는 다음 수명 경로를 검증합니다.

- 실제 asio::post 실행과 io_context 폐기, 예약 도중 예외 시 대기량 회수.
- co_await 중 coroutine frame 폐기 시 in-flight 회수.
- 실패한 write의 partial byte와 성공/실패/폐기의 구분, 중복 정리 방지.
- 네 생산 thread에서의 예약·해제 합계와 종료 후 gauge 0.
- 중복 Session ID 거절과 원래 계측 보존, 종료 이력 128개 제한과 high-water 보존.

기존 Session 통합 테스트는 실제 ping, 분할·연속 packet, protocol 오류, 입장·이동·퇴장을 수행한 뒤 정확한 송신
byte와 packet 수, command·전달 수, Registry/Room 상태 및 모든 Session gauge의 정리를 확인합니다.
RoomWorker callback 실패도 기존 summary와 실시간 counter가 일치하는지 검사합니다.

전체 실행 파일과 JSON 출력의 검증은 다음 명령으로 재현합니다. 프로세스 시작과 부하 실행에 timeout이 있으며,
종료 정리는 이 스크립트가 시작한 프로세스만 대상으로 합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-server-metrics.ps1
```

고정 시나리오는 8 clients, 각 warm-up 20회, 측정 2,000회입니다. 확인할 값은 다음과 같습니다.

| 항목 | 기대값과 근거 |
|---|---|
| Move | 8 × (20 + 2,000) + barrier 1 = 16,161 |
| Room command | Move + join 8 + leave 8 = 16,177 |
| Delivery | Move × 8 + join fan-out 36 + leave fan-out 28 = 129,352 |
| 송신 byte | (Move × 8 + 36) × 22 + 28 × 16 = 2,845,576 |
| 종료한 Session | 8개, 각각 post/Queue/in-flight gauge 모두 0 |
| 서버 상태 | 연결 0개, Room 1개가 계속 실행 중 |

최종 검증 결과와 batch 관측값은 실행 후 남기는 `out/metrics-validation/<시각>/verified-snapshot.json`으로 확인합니다.
2026-09-26 Release 검증에서는 위 기대값이 모두 일치했고, 129,352개 packet이 48,579개의 batch에 들어갔습니다.
평균은 약 2.663 packet/batch, 관측 최대는 8개였습니다. 해당 실행의
[최종 JSON](observability/2026-09-26-server-metrics-smoke.json)을 함께 보관합니다. CTest 9개가 모두 통과했고,
계측 비활성화 상태의 기존 실행 방식 및 독점 사용 중인 포트에서 시작 실패 시 reporter도 종료되는 것을 확인했습니다.

이 검증은 성능 개선 배수를 산출하는 실험이 아닙니다. 계측·출력 비용과 스케줄링에 따라 batch 크기는 달라집니다.
스크립트 끝에서는 서버 프로세스를 종료하므로 아직 graceful shutdown 검증으로 간주하지 않습니다.

## 다음 단계

한 client의 수신 중단과 제한된 생산 burst로 post 앞 대기와 Queue·in-flight가 어떻게 증가하는지 관찰합니다.
실제 제한을 추가할 때에는 예약 전 admission과 취소 시 budget 반환을 설계하고, Leave 같은 필수 정리 작업이
제한 때문에 사라지지 않는지 검증합니다. 현재 Room command Queue도 별도의 무제한 대기 지점이므로 전달 경로
세 gauge만으로 서버 전체 메모리 한도를 주장하지 않습니다.
