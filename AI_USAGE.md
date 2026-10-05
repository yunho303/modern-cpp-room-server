# AI Usage

이 프로젝트는 AI를 숨기지 않고, 개발자가 판단과 검증을 소유하는 방식으로 사용합니다.

## Current usage

- 초기 CMake 구성과 저장소 구조 초안
- 패킷 코덱 구현 초안
- 오류 및 경계 조건 테스트 후보 도출
- coroutine TCP Session과 loopback 통합 테스트 초안
- single-owner Room Worker와 Session command 변환 초안
- Room Event broadcast, Session Registry와 bounded outbound queue 초안
- benchmark scenario, percentile/throughput metrics module and tests
- coroutine load client and Room broadcast benchmark runner
- repeatable benchmark matrix script and CSV aggregation
- 설계 문서의 구조 정리
- 사용자 제공 보강안과 현재 코드를 대조한 v0.2 계획 및 4주차 코드 질의응답 정리
- 서버 계측, RAII 기반 대기량 정리, 실제 연결 통계 검증 코드 작성 및 실행 지원

## 2026-09 보강 작업의 실제 역할

- 사용자가 TCP_NODELAY가 기존 결과에 영향을 줄 수 있다는 문제와 운영 안정성 보강 방향을 제시했습니다.
- Codex가 C/D 조건의 재측정, 전달 경로 계측 구현 및 자동 검증을 수행했습니다. 네 조건 전체 비교나 사용자 본인의
  직접 구현·실행 경험으로 확대해 서술하지 않습니다.
- [v0.2 계획](docs/v0.2-operational-plan.md)과 [계측 검증](docs/server-metrics.md)은 구현 근거와 남은 한계를
  구분한 기록입니다. 아래 Developer responsibility는 검토·학습 책임이며, 각 작업을 이미 직접 수행했다는
  이력 선언으로 사용하지 않습니다.

## Developer responsibility

- 프로젝트 목표와 범위 결정
- 자료구조, 객체 수명, 스레드 소유권과 오류 정책의 최종 결정
- 생성된 코드를 한 줄씩 검토하고 직접 수정
- Debug/Release 빌드, 테스트와 프로파일링 결과 검증
- 면접에서 모든 코드와 선택하지 않은 대안을 설명

AI가 생성한 첫 결과는 완료된 코드로 간주하지 않습니다. 직접 설명하거나 변경할 수 없는 코드는 프로젝트에
남기지 않으며, 성능 개선은 AI의 추측이 아니라 동일 조건의 측정 결과로 판단합니다.
