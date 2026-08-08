# Week 1 Review Checklist

다음 질문에 코드 없이 답하고, 관련 구현을 직접 변경할 수 있어야 첫 주 작업이 완료된 것입니다.

- TCP read 횟수와 패킷 개수가 일치하지 않는 이유는 무엇인가?
- `PacketView`가 payload를 소유하지 않도록 한 이유와 수명 위험은 무엇인가?
- `optional` 대신 `expected`를 사용한 이유는 무엇인가?
- 구조체 `reinterpret_cast` 대신 field별 `memcpy`를 사용한 이유는 무엇인가?
- network byte order를 고정해야 하는 이유는 무엇인가?
- payload 크기를 메모리 할당 전에 검사하는 이유는 무엇인가?
- incomplete input과 invalid input을 구분해야 하는 이유는 무엇인가?
- 여러 패킷이 한 수신 버퍼에 있을 때 `consumed_bytes`를 어떻게 사용하는가?

## Hands-on changes

- 최대 payload를 32 KiB로 변경하고 테스트도 함께 수정합니다.
- 새로운 PacketType을 추가하고 unknown type 테스트가 계속 유효한지 확인합니다.
- sequence를 64-bit로 변경할 때 wire format과 테스트가 어떻게 달라지는지 설명합니다.
