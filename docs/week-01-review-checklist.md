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

- [x] TCP가 순서를 보장하는데 현재 `sequence`가 왜 필요한지 검토하고, 구체적인 용도가 없어 wire format과
  구현에서 제거했습니다.
- [ ] 최대 payload를 32 KiB로 변경한다면 함께 수정해야 할 코드와 테스트를 설명합니다.
- [ ] PacketType의 underlying type을 32-bit로 변경할 때 wire format과 테스트가 어떻게 달라지는지 설명합니다.

단순히 enum 값을 하나 추가하는 작업은 현재 설계 이해를 더 보여주지 않으므로 필수 과제에서 제외했습니다.
