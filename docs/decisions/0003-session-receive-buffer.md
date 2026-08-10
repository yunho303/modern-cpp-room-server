# 0003. Session-owned receive buffer

## Status

Accepted

## Context

TCP는 패킷 경계를 보존하지 않습니다. 한 패킷이 여러 read로 나뉘거나 여러 패킷이 한 read에 함께 들어올 수
있으므로 Session은 아직 해석하지 못한 byte를 다음 read까지 보관해야 합니다.

## Decision

Session마다 하나의 `ReceiveBuffer`를 소유합니다. 생성할 때 Session당 최대 누적 byte를 반드시 지정합니다.
네트워크 read가 끝나면 받은 byte를 추가하고 다음 순서로 처리합니다.

1. `append`가 최대 누적 크기를 초과하면 Session을 종료합니다.
2. `readable_bytes()`를 `decode_one`에 전달합니다.
3. 완성된 `PacketView`는 버퍼를 변경하기 전에 처리합니다.
4. 처리한 패킷의 `consumed_bytes`만큼 `consume`합니다.
5. incomplete 오류라면 byte를 보존하고 다음 read를 기다립니다.
6. invalid 오류라면 입력을 소비하지 않고 Session이 연결을 종료합니다.

`ReceiveBuffer`는 byte의 저장과 소비만 담당하고 packet 규칙을 알지 않습니다. 반대로 protocol codec은 입력
메모리를 소유하지 않습니다. 이를 통해 네트워크 저장 책임과 protocol 해석 책임을 분리합니다.

## Lifetime rule

`PacketView::payload`는 `ReceiveBuffer` 내부 메모리를 가리킵니다. 따라서 view는 다음 `append` 또는 `consume`
호출 전까지만 유효합니다. 현재 단계에서는 packet handler가 동기적으로 payload를 읽고 반환한 뒤에만 byte를
소비합니다. 이후 Room thread로 작업을 넘길 때는 필요한 값을 소유하는 command로 변환해야 합니다.

## Consequences

- 패킷이 분할되거나 결합되어 도착해도 같은 처리 흐름을 사용합니다.
- 소비할 때마다 vector 앞부분을 지우지 않고 offset만 이동합니다.
- 뒤쪽 여유 공간이 부족할 때만 남은 byte를 앞으로 이동해 반복적인 복사를 줄입니다.
- 초기 capacity는 메모리 예약량일 뿐이므로, 별도의 최대 누적 크기로 무제한 성장을 방지합니다.
- Session이 view를 비동기 작업에 그대로 저장하면 dangling view가 되므로 금지합니다.

## Alternative considered: ring buffer

ring buffer는 앞 공간을 재사용하기 위한 `memmove`를 피할 수 있습니다. 하지만 packet이 저장 공간의 끝과 처음에
걸치면 현재처럼 하나의 연속된 `span`을 받는 decoder를 그대로 사용할 수 없습니다. 두 구간을 해석하도록 codec을
복잡하게 만들거나 끊어진 packet을 별도 버퍼에 복사해야 합니다.

현재 vector와 offset 방식을 baseline으로 유지하고, 부하 테스트에서 compact 횟수와 이동한 byte가 실제 병목인지
측정합니다. 병목으로 확인될 때 ring buffer를 구현해 동일한 조건에서 성능을 비교합니다.
