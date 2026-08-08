# Week 1 Learning Notes

## 이번 주에 이해한 큰 흐름

현재 구현은 서버 전체가 아니라 TCP byte stream 위에서 패킷 경계를 찾아내는 protocol 계층입니다.
`encode_packet`은 정해진 wire format으로 byte 배열을 만들고, `decode_one`은 수신 버퍼에서 완성된 패킷 하나를
해석합니다. TCP의 read 경계와 패킷 경계가 일치하지 않기 때문에 incomplete 결과와 `consumed_bytes`가 필요합니다.

## 코드를 읽으며 확인한 C++ 기능

- `enum class`는 정수와 암묵적으로 섞이지 않아 PacketType의 사용 범위를 명확히 합니다.
- `std::span`은 메모리를 소유하지 않는 연속 영역 view입니다. `const span<byte>`와 `span<const byte>`는 다릅니다.
- `std::expected`는 성공 값뿐 아니라 실패 이유를 함께 표현해 incomplete와 invalid 입력을 구분합니다.
- `std::byte`는 payload가 문자가 아니라 가공되지 않은 byte임을 타입으로 표현합니다.
- `std::endian`과 `std::byteswap`은 wire의 big-endian 값과 host 표현을 변환합니다.
- `noexcept`는 함수 내부에서 오류가 생기지 않는다는 뜻이 아니라 예외가 밖으로 나오지 않는다는 계약입니다.
- designated initializer는 aggregate의 멤버 이름을 지정해 초기화 의도를 드러냅니다.
- defaulted `operator==`는 멤버별 동등 비교를 컴파일러가 생성하게 합니다.

## CMake에서 확인한 구조

Visual Studio solution은 원본이 아니라 CMake가 생성한 결과입니다. 실제 빌드 구조의 기준은 `CMakeLists.txt`입니다.
`mcrs_protocol`은 정적 라이브러리, `mcrs_server`와 `mcrs_protocol_tests`는 실행 파일입니다.
`mcrs_project_options`는 결과 파일을 만드는 라이브러리가 아니라 C++23과 warning 설정을 전달하는
INTERFACE target입니다. PUBLIC 설정은 현재 target이 사용하면서 이를 연결한 다음 target에도 전파됩니다.

## 리뷰가 실제 변경으로 이어진 부분

초기 header에는 `sequence`가 들어 있었습니다. 코드를 읽으며 "TCP가 이미 순서를 보장하는데 이 값은 어디에
사용하는가?"라는 의문을 제기했고, 현재 요구사항에는 중복 제거, 재접속 복구, 응답 매칭처럼 sequence가 해결할
문제가 없다는 결론을 내렸습니다. 사용 목적이 없는 필드는 유지 비용만 늘리므로 header를 10 bytes에서 6 bytes로
줄이고 구현, 테스트, 문서를 함께 수정했습니다.

이 판단은 sequence가 항상 불필요하다는 뜻이 아닙니다. 이후 실제 요구사항이 생기면 그 용도와 보장 범위를 먼저
정의한 뒤 다시 도입합니다. 필요할 것 같다는 이유만으로 protocol에 필드를 미리 넣지 않는 것이 이번 리뷰의 핵심입니다.

## 현재 이해 수준과 다음 과제

패킷 포맷, encode/decode 과정, 오류 분류, view의 수명 위험과 빌드 target 구성은 코드 흐름을 따라 설명할 수
있습니다. 다음 단계에서는 실제 TCP Session이 여러 번의 read 결과를 누적하고, 완성된 패킷만 반복해서 꺼내는
과정을 구현하며 이 지식을 네트워크 동작으로 연결합니다.
