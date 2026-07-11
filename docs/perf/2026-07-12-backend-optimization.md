# 2026-07-12 백엔드 처리 경로 최적화 측정 기록

BiQuad 커널 배분, convolution 파티션 slab, 인터리브 I/O 변환 세 가지를
고친 캠페인의 측정 기록입니다. 각 수정에는 재발을 막는 테스트가 붙어
있습니다(아래 '회귀 방지' 절 참조). 수치는 특정 기기 한 대의 참고값이며,
회귀 판정은 절대값이 아니라 함께 커밋된 테스트가 담당합니다.

## 측정 환경

- CPU: Intel Core i5-12600KF (AVX2, AVX-512 없음), Windows 11
- 빌드: x64 Release, AVX2 변형(로컬 기본값), VS18 BuildTools v145
- 실행: P-코어 고정(affinity), High 우선순위, 48 kHz, 블록 480프레임
- 시나리오: `Benchmark/scenarios/` (IR은 `New-BenchIr.ps1`로 생성)

재현 명령 예시는 다음과 같습니다.

```powershell
Benchmark\scenarios\New-BenchIr.ps1
Benchmark\x64\Release\Benchmark.exe --nopause -r 48000 --batchsize 480 `
  -l 120 -c 2 --repeat 3 --profile --config-path Benchmark\scenarios\biquad_chain.txt
```

## Benchmark 시나리오 결과 (한 코어 CPU 부하, 블록 시간 µs)

| 시나리오 | 지표 | 수정 전 | 수정 후 |
|---|---|---:|---:|
| biquad_chain 2ch | CPU 부하 | 0.437% | 0.248% |
| biquad_chain 2ch | BiQuadFilter::process 평균/호출 | 2.04 µs | 1.12 µs |
| biquad_chain 8ch | CPU 부하 | 0.971% | 0.948% |
| biquad_chain 8ch | BiQuadFilter::process 평균/호출 | 4.55 µs | 4.48 µs |
| conv_long 2ch | CPU 부하 | 2.460% | 2.396% |
| conv_long 2ch | 블록 중앙값 / p99 | 219 / 2280 µs | 230 / 2239 µs |
| io_passthrough 2ch | read+write 평균/블록 | 0.39+0.45 µs | 0.25+0.14 µs |
| io_passthrough 8ch | read+write 평균/블록 | 1.96+1.69 µs | 1.48+1.68 µs |

요약하면 스테레오 BiQuad 체인이 가장 크게 좋아졌고(dual-chain ILP,
체인 전체 약 43% 감소), I/O 변환은 스테레오에서 read 1.6배/write 3.2배
빨라졌습니다. convolution slab은 이 기기(워킹셋 약 1.5 MB가 캐시에 수용)
에서는 평균 약 2~3% 수준이며, 효과의 본체는 파티션 수가 더 많거나 캐시가
작은 환경에서의 DTLB/프리페치 거동과 초기화 시 pre-touch(첫 사용 소프트
페이지 폴트 제거)에 있습니다. 8채널 인터리브는 Highway 인터리브 연산이
없어 기존 스칼라 경로를 유지했으므로 변화가 없습니다.

## 회귀 방지

- **BiQuad 커널 배분**: `BiQuadKernelTests`(HybridConvTests 내)가
  planBiQuadKernels의 채널 커버리지·SIMD 그룹 고정·단일 체인 최대 1채널
  계약과, 다채널 출력의 모노 대비 비트 동일성을 고정합니다.
- **Convolution slab**: `assertPartitionBuffersFormOneSlab`(HybridConvTests)이
  파티션 평면의 연속 slab 배치(균일 stride, real/imag 인접, 64B 정렬)를
  고정합니다.
- **인터리브 I/O**: `SampleIoTests`(EngineOrchestrationTests 내)가 모든 변환
  경로의 스칼라 참조 대비 비트 동일성과, 스테레오 float write의 나이브
  스칼라 대비 처리량 하한을 고정합니다.

## 참고: 인터리브 I/O 계약이 write 방향인 이유

read 방향(strided load)은 상수 채널 특수화 덕에 MSVC 자동 벡터화가 이미
개입해, 명시 SIMD 전과 후를 처리량으로 안정적으로 구분할 수 없었습니다
(AVX2에서 구 구현이 나이브 참조 대비 이미 2.07배). write 방향(strided
store)은 자동 벡터화가 미치지 못해 신/구 구현이 크게 갈리므로 계약으로
삼기에 적합합니다. 구체 수치는 아래 표에 있습니다.

| 지표 (스테레오 float, 480프레임, 블록당) | 구 구현 | 신 구현 |
|---|---:|---:|
| AVX2 read | 195 ns (2.09x) | 64 ns (6.37x) |
| AVX2 write | 195 ns (2.08x) | 98 ns (4.16x) |
| SSE2 read | 195 ns (2.09x) | 217 ns (1.88x) |
| SSE2 write | 195 ns (2.10x) | 150 ns (2.72x) |

괄호는 나이브 스칼라 참조(~408 ns) 대비 배율입니다. SSE2 read는 2-lane
인터리브 셔플 비용 탓에 자동 벡터화된 구 구현보다 22 ns 느려지지만, write
이득이 그보다 크고 절대값이 블록당 수십 ns 수준이라 타깃별 분기 없이 단일
Highway 소스를 유지했습니다. write 계약 임계값 2.4배는 구 구현(≤2.10)과
신 구현(≥2.72) 사이에 놓인 값입니다.
