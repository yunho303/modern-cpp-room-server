# 0001. C++23 language mode with selected features

## Status

Accepted

## Context

이 프로젝트는 최신 문법을 나열하기보다 객체 수명, 오류와 동시성 문제를 더 명확하게 표현하는 것이 목적입니다.
현재 로컬 도구체인은 Visual Studio 2022 17.8, MSVC 19.38입니다.

## Decision

CMake의 `cxx_std_23`을 요구합니다. 기능은 문제에 이유가 있을 때만 사용합니다.

- `std::span`: 수신 버퍼를 복사하지 않는 비소유 view
- `std::expected`: 정상값과 복구 가능한 오류의 명시적 분리
- `concept`: wire format에서 허용하는 정수 타입 제한
- `std::byteswap`: 명시적인 network byte order 변환
- `std::source_location`: 테스트 실패 위치 기록

향후 `std::jthread`, `std::stop_token`과 C++20 Coroutine은 Session과 Worker 종료 경로에 적용합니다.

## Consequences

- C++23 표준 라이브러리 지원이 충분한 컴파일러가 필요합니다.
- 최신 기능을 사용했다는 이유만으로 기존의 단순한 코드보다 복잡하게 만들지 않습니다.
- 다른 컴파일러를 CI에 추가할 때 각 기능의 실제 지원 여부를 빌드로 검증합니다.
