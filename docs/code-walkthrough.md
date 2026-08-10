# Code Walkthrough

## 1. Current execution flow

현재 단계에는 실제 TCP 연결이 없습니다. `main`이 작은 payload를 packet으로 encode하고, 같은 packet을 다시
decode하여 protocol 계층이 독립적으로 동작하는지만 보여줍니다.

```text
payload
  -> encode_packet
  -> [payload size | packet type | payload]
  -> decode_one
  -> PacketView
```

다음 단계에서 Boost.Asio Session이 수신 버퍼를 `decode_one`에 전달하게 됩니다.

## 2. Build structure

`CMakeLists.txt`는 다음 target을 만듭니다.

- `mcrs_project_options`: C++23과 compiler warning 설정을 전달하는 interface target
- `mcrs_protocol`: packet codec을 담는 library
- `mcrs_network`: Session에서 사용할 수신 버퍼를 담는 library
- `mcrs_server`: protocol library를 사용하는 executable
- `mcrs_protocol_tests`: CTest가 실행하는 test executable
- `mcrs_receive_buffer_tests`: 분할 및 결합 수신을 검증하는 test executable

경고를 오류로 처리해 narrowing conversion, shadowing과 비표준 코드가 조용히 들어오는 것을 막습니다.
`CMakePresets.json`은 Visual Studio 2022 x64의 configure, Debug/Release build와 test 명령을 고정합니다.

## 3. Public protocol types

### PacketType

wire에 기록되는 16-bit packet 식별자입니다. `enum class`이므로 정수나 다른 enum과 암묵적으로 섞이지
않습니다. 현재 값은 이후 Session과 Room command를 구현하기 위한 최소 목록입니다.

### PacketHeader

논리적인 header 표현입니다. 실제 메모리 크기는 padding 때문에 6 bytes가 아닐 수 있으므로 구조체 자체를
전송하지 않습니다. 각 field를 고정된 offset에 별도로 기록합니다. 기본 `operator==`는 멤버 선언 순서대로
동등 비교 코드를 생성하므로 테스트에서 두 header를 한 번에 비교할 수 있습니다.

### PacketView

decode 결과입니다. `payload`는 `std::span<const std::byte>`이므로 메모리를 소유하거나 복사하지 않습니다.
입력 버퍼의 일부를 바라보기만 하므로 빠르지만, 입력 버퍼가 재할당되거나 사라지면 view도 무효가 됩니다.
Session은 packet 처리가 끝날 때까지 수신 버퍼의 수명을 보장해야 합니다.

`consumed_bytes`는 하나의 TCP 수신 버퍼에 packet 여러 개가 붙어 왔을 때 첫 packet만 제거하고 나머지를 다시
decode하기 위해 필요합니다.

### DecodeError and EncodeError

예외 대신 `std::expected`의 error 타입으로 사용합니다. `incomplete_header`와 `incomplete_payload`는 다음 read를
기다리면 복구할 수 있습니다. 과대 payload와 알 수 없는 packet type은 protocol 위반이므로 연결 종료 후보입니다.
단순히 값이 없다는 사실만 표현하는 `optional`보다 실패 이유까지 전달하는 `expected`가 적합합니다.

## 4. Endian helpers

`WireInteger` concept은 endian 변환 함수에 2, 4, 8-byte unsigned integer만 들어오도록 compile time에 제한합니다.
`read_big_endian`과 `write_big_endian`은 `memcpy`로 값을 옮긴 뒤 little-endian host에서 `std::byteswap`합니다.

수신 주소를 정수 pointer로 `reinterpret_cast`하지 않는 이유는 alignment와 strict aliasing 문제를 피하기
위해서입니다. wire format을 big endian으로 고정했기 때문에 서로 다른 CPU byte order에서도 같은 packet을
해석할 수 있습니다.

## 5. decode_one

검증 순서는 다음과 같습니다.

1. 고정 header 6 bytes가 도착했는지 확인합니다.
2. payload size와 packet type을 big endian으로 읽습니다.
3. payload size가 64 KiB 제한을 넘는지 검사합니다.
4. 허용된 packet type인지 검사합니다.
5. header에 적힌 payload 전체가 도착했는지 확인합니다.
6. 성공하면 입력 버퍼를 가리키는 `PacketView`를 반환합니다.

크기와 타입을 먼저 검증하므로 공격자가 큰 size를 보내도 payload용 메모리를 할당하지 않습니다. 함수는 입력을
수정하지 않고 동적 할당도 하지 않으므로 `noexcept`입니다.

## 6. encode_packet

payload 크기와 packet type을 검증한 뒤 header와 payload를 담을 `std::vector<std::byte>`를 한 번 할당합니다.
header field는 big endian으로 기록하고 payload는 `memcpy`합니다.

현재 encoder가 packet마다 vector를 할당하는 것은 의도적인 baseline입니다. 실제 부하 테스트에서 할당이
병목으로 확인되면 buffer reuse, immutable shared buffer 또는 `std::pmr`을 적용하고 전후 수치를 비교합니다.
측정 전에 메모리 풀을 넣지 않습니다.

## 7. Demo executable

`src/main.cpp`는 2-byte payload를 `ping` packet으로 encode하고 다시 decode합니다. `expected`를 boolean처럼
검사하고 실패 시 `error()`를 문자열로 출력합니다. 성공 시 packet type과 payload 크기를 출력합니다.

이 코드는 서버가 아닙니다. protocol library의 가장 작은 사용 예이며, 다음 단계에서 Coroutine Session을
실행하는 entry point로 교체합니다.

## 8. Tests

외부 test dependency 없이 CTest에서 실행 가능한 작은 runner를 두었습니다. `std::source_location`은 실패한
expression의 파일과 line을 출력합니다.

현재 검증 항목은 다음과 같습니다.

- encode/decode 왕복 시 header와 payload 보존
- wire header의 big-endian byte 배열
- header가 나뉘어 도착한 경우
- payload가 나뉘어 도착한 경우
- 64 KiB를 초과하는 payload 거부
- 알 수 없는 packet type 거부
- 한 buffer에 연속된 두 packet 처리
- payload가 없는 packet 처리

테스트에서 `reserve` 후 두 packet을 이어 붙이는 것은 실제 TCP stream에서 packet 경계가 보존되지 않는 상황을
재현하기 위한 것입니다.

## 9. CI and documentation

`.github/workflows/ci.yml`은 push와 pull request에서 Linux Release build와 CTest를 실행합니다. 로컬 MSVC
빌드와 함께 다른 compiler에서도 표준에 맞는지 확인하기 위한 장치입니다.

`docs/decisions`에는 기술을 사용한 이유와 대가를 기록합니다. `AI_USAGE.md`는 AI가 만든 초안과 개발자가 직접
결정하고 검증해야 하는 범위를 공개합니다.

## 10. ReceiveBuffer

`ReceiveBuffer`는 TCP read로 받은 byte를 소유하지만 packet 형식은 알지 않습니다. `readable_bytes()`로 아직
처리하지 않은 영역의 `span`을 제공하고, Session은 `decode_one`이 반환한 `consumed_bytes`만큼만 소비합니다.
생성 시 최대 누적 크기를 필수로 받아 초기 `reserve` 크기와 실제 허용 한도를 혼동하지 않도록 했습니다.
`append`는 한도를 넘으면 `expected`로 오류를 반환하고 기존 byte는 변경하지 않습니다.

vector 앞부분을 매번 `erase`하면 남은 모든 byte가 매번 이동합니다. 대신 `read_offset_`만 증가시키고, 새 byte를
추가할 뒤쪽 공간이 부족할 때만 `memmove`로 남은 영역을 앞으로 당깁니다. 전체 packet을 소비하면 size는 0으로
만들되 예약된 capacity는 다음 read에서 재사용합니다.

`readable_bytes()`가 반환한 view는 `append`나 `consume`이 호출되면 무효가 될 수 있습니다. Room worker처럼
더 오래 실행되는 흐름에 넘길 때는 payload view를 보관하지 않고 필요한 값을 소유하는 command로 변환해야 합니다.

## 11. Known gaps

- 실제 TCP accept와 Session이 아직 없습니다.
- 수신 버퍼의 보존, 압축과 backpressure 정책이 아직 없습니다.
- packet payload schema와 command dispatch가 없습니다.
- vector 할당 비용의 기준 성능을 아직 측정하지 않았습니다.
- Linux CI에서도 빌드와 테스트를 통과했지만, sanitizer와 실제 네트워크 부하 테스트는 아직 없습니다.
