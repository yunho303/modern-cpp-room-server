# 2026-09-22 Windows TCP_NODELAY 통제 재측정

## 검증할 질문

기존 송신 배치 전후 비교는 서버와 부하 클라이언트에서 `TCP_NODELAY`를 명시적으로 설정하거나 읽어 확인하지
않았습니다. 따라서 기존 9.15배 개선을 application write 비용 감소만으로 설명하기 전에, Nagle 알고리즘을
비활성화한 조건에서도 배치 효과가 유지되는지 확인합니다.

`TCP_NODELAY=true`는 Nagle 알고리즘을 비활성화합니다. 설정은 연결의 각 송신 측에 적용하므로 서버의 accepted
socket과 부하 클라이언트의 connected socket 모두 설정합니다.
[Asio 1.36.0 문서](https://think-async.com/Asio/asio-1.36.0/doc/asio/reference/ip__tcp/no_delay.html)

## 비교 대상과 조건

| 항목 | 설정 |
|---|---|
| 배치 전 | `f53903456b01fb5370346f1f8c7c2f3d84efec70` |
| 배치 후 | `daf639baedb5f2f629243778d5d2e7a683feddbd` |
| 서버 / 클라이언트 TCP_NODELAY | 모두 `true`; 최초 application packet 송수신 전에 `get_option`으로 확인 |
| CPU / OS | AMD Ryzen 5 3500X / Windows 10.0.26200.0 |
| Compiler / Build | MSVC 19.38.33133.0 x64 / Release |
| CMake / Asio | 3.30.2 / standalone Asio 1.36.0 |
| Clients | 1, 8, 16, 32, 64 |
| 반복 | 조건마다 5회, 홀수 회차는 배치 전부터, 짝수 회차는 배치 후부터 실행 |
| Warm-up / 측정 | client당 100 / 2,000 Move |
| Flow control | client당 진행 중인 Move 1개 |
| Latency | 자기 PlayerMovedEvent 수신까지의 end-to-end 시간, nearest-rank percentile |
| 대표값 | 실행별 처리량 및 percentile의 5회 중앙값 |
| Topology | localhost의 별도 서버·부하 클라이언트 프로세스, 각각 I/O thread 1개 |

두 커밋을 `git archive`로 독립 디렉터리에 풀고 같은 socket 설정/확인 코드만 추가했습니다. 현재 작업 폴더의
미커밋 변경은 측정에 포함하지 않았습니다. 배치 전은 원래 packet별 `pop`/`async_write` 경로를 사용하며,
배치 후의 batch 크기를 1로 제한해 흉내 내는 방식은 사용하지 않았습니다.

두 Release 빌드에서 각각 기존 CTest 8개가 모두 통과한 뒤 측정했습니다. 각 실행은 새 서버를 시작하고 정상적인
Room 입장·warm-up·측정·순차 퇴장을 수행한 다음 서버를 종료합니다. 매 실행의 표본 수가 `clients * 2,000`인지
검사하고, 연결/프로토콜 오류 또는 비정상 종료 시 실행을 실패 처리합니다.

## 결과

50회 실행이 모두 성공했으며, 총 2,420,000개의 측정 표본을 확인했습니다. 모든 실행에서 목표 표본 수와
양쪽 `TCP_NODELAY=1` 조건을 충족했고, 50개의 서버 오류 로그는 모두 비어 있었습니다. PowerShell로 계산한
10개 조건별 요약을 Python `statistics.median`으로 별도 계산해 일치함을 확인했습니다.

아래 처리량과 percentile은 각 조건의 5회 중앙값입니다. 처리량 비율은 두 중앙값의 비율이며,
p99 열은 전체 실행의 표본을 합친 p99가 아니라 실행별 p99의 중앙값입니다.

| Clients | Commands/s before | Commands/s after | Ratio | p50 before | p50 after | p99 before | p99 after |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 13,891.416 | 14,182.768 | 1.02x | 0.062 ms | 0.062 ms | 0.233 ms | 0.243 ms |
| 8 | 6,310.469 | 13,924.960 | 2.21x | 1.157 ms | 0.520 ms | 2.579 ms | 1.370 ms |
| 16 | 3,347.741 | 12,503.542 | 3.73x | 4.594 ms | 1.205 ms | 7.339 ms | 2.663 ms |
| 32 | 1,688.572 | 10,357.970 | 6.13x | 18.398 ms | 2.879 ms | 27.734 ms | 5.708 ms |
| 64 | 825.428 | 6,326.567 | 7.66x | 75.414 ms | 9.647 ms | 112.418 ms | 19.354 ms |

64 clients에서 처리량은 **7.66배**, p50은 **87.2% 감소**, p99는 **82.8% 감소**했습니다. Nagle을 비활성화한
조건에서도 배치의 이점이 관측됐으므로, 배치 이점 전체가 Nagle 활성 상태에만 의존하는 현상은 아니었습니다.
반면 1 client의 처리량은 1.02배로 비슷하며 p99는 0.233 ms에서 0.243 ms로 증가했습니다. 모든 부하 구간의
모든 지연 지표가 개선됐다고 주장하지 않습니다.

원본 실행값은 [raw CSV](2026-09-22-windows-nodelay-comparison-raw.csv), p95를 포함한 요약은
[summary CSV](2026-09-22-windows-nodelay-comparison-summary.csv)에 있습니다. 과거의 9.15배는 최초 측정 기록으로
보존하고, Nagle을 통제한 배치 효과를 설명할 때는 이번 7.66배 결과와 위 조건을 함께 제시합니다.

### 포트폴리오 설명 예시

> 패킷별 송신을 bounded gather write로 바꾼 최초 측정에서는 처리량이 9.15배 증가했습니다. 이후 Nagle이 결과에
> 영향을 줄 가능성을 고려해 서버와 부하 클라이언트 모두 TCP_NODELAY를 설정하고 배치 전후를 다시 비교했습니다.
> localhost 64클라이언트 조건의 5회 중앙값에서 처리량 7.66배 증가와 p99 지연 112.418 ms → 19.354 ms를 확인했습니다.

## 해석 범위

- 이번 실험은 **Nagle 비활성화 상태에서의 배치 전후 비교**입니다. Nagle 활성/비활성의 같은 날 4조건 실험은
  아니므로, 과거 9.15배 중 얼마가 Nagle 때문인지를 수치로 분해할 수는 없습니다.
- 과거 결과와 날짜 및 서버 재시작 방식이 다릅니다. 기존 값과 이번 값의 차이를 Nagle 하나의 효과로 해석하지
  않습니다. 이번 두 버전끼리는 같은 재시작 방식과 측정 조건을 사용합니다.
- TCP_NODELAY는 delayed ACK, OS scheduling, socket buffering, client parsing 등의 영향을 모두 제거하는
  설정이 아닙니다. 배치 효과가 남더라도 순수한 application CPU 비용 또는 syscall 수만의 효과라고 단정하지
  않습니다. 네트워크 동작과 application 송신 구조가 상호작용할 수 있다는 배경은
  [Microsoft Winsock 문서](https://learn.microsoft.com/en-us/windows/win32/winsock/tcp-ip-specific-issues-2)를 참고합니다.
- 동일 PC에서 서버와 부하 클라이언트가 CPU를 공유합니다. localhost 상대 비교이며 production capacity나 실제
  인터넷 환경의 지연을 나타내지 않습니다.
- `EstimatedDeliveriesPerSecond`는 `commands/s * clients`이며 실제 socket completion 개수를 센 값이 아닙니다.

## 재현

Visual Studio 2022, CMake, Git, PowerShell 환경에서 저장소 루트에서 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-nodelay-comparison.ps1
```

스크립트는 두 커밋의 측정용 복사본을 `out/nodelay-comparison/<실행 시각>`에 생성하고 설정·빌드·CTest·성능 측정을
순서대로 실행합니다. 기존 Asio checkout이 있으면 재사용하고, 없으면 각 커밋에 고정된 의존성을 가져옵니다.
최종 CSV의 `ServerTcpNoDelay`와 `ClientTcpNoDelay`는 모두 1이어야 합니다.

이 스크립트의 설정 변경은 측정용 복사본에 적용됩니다. 현재 작업 폴더의 서버·클라이언트 구현은 변경하지 않습니다.
