# 0002. Explicit length-prefixed packet framing

## Status

Accepted

## Context

TCP는 한 번의 read와 한 패킷의 경계가 일치하지 않습니다. 헤더와 payload가 나뉘어 도착하거나 여러 패킷이
한 번에 도착할 수 있으므로 구조체를 수신 버퍼에 바로 캐스팅할 수 없습니다.

## Decision

wire header는 padding에 영향을 받지 않는 10 bytes 고정 포맷으로 정의합니다.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | payload size, big endian |
| 4 | 2 | packet type, big endian |
| 6 | 4 | sequence, big endian |

파서는 `std::span<const std::byte>`를 받아 다음 결과를 반환합니다.

- 완성된 `PacketView`와 소비한 byte 수
- 헤더 또는 payload가 덜 도착한 상태
- 크기 제한 초과 또는 알 수 없는 패킷 타입

payload 최대 크기는 64 KiB입니다. 크기 제한과 타입을 검증한 후에만 payload를 사용합니다.

## Consequences

- `PacketView`의 payload는 입력 버퍼보다 오래 살 수 없습니다.
- Session은 incomplete 결과에서 연결을 끊지 않고 다음 read를 기다려야 합니다.
- 잘못된 크기와 타입은 추가 데이터를 기다리지 않고 protocol error로 처리할 수 있습니다.
- baseline encoder는 패킷마다 `std::vector`를 할당합니다. 향후 측정에서 병목이 확인되면 버퍼 재사용을 검토합니다.
