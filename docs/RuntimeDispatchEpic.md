# B안 — SIMD 런타임 dispatch 단일 바이너리화 (Highway HWY_DYNAMIC_DISPATCH)

> 이 문서는 GitHub 이슈를 대신한다. 저장소에 이슈 기능이 꺼져 있어 같은 내용을 `docs/`에 영구 기록으로 남긴다. 이슈를 켜면 그대로 옮길 수 있다.

## 개요

A안(SIMD를 Google Highway로 이식, **정적 변형별 dispatch**)은 PR #43으로 머지돼 **v1.15.0**으로 배포됐다. 지금은 x64 5변형(sse2/avx/avx2/avx512/avx10.1) + arm64 neon이 각각 별도 바이너리이고, 각 변형을 자기 `/arch`로 컴파일하면 Highway가 그 ISA로 커널을 낸다.

이 문서(**B안**)는 그 후속이다. **x64 변형들을 런타임 CPU 감지로 최적 커널을 고르는 단일 바이너리로 통합**한다(Highway `HWY_DYNAMIC_DISPATCH`). ARM64는 별도 바이너리 유지. 목표는 사용자가 변형을 직접 고르지 않아도 되는 단일 x64 설치 파일과 단일 업데이트 채널이다.

> [!IMPORTANT]
> **이 작업은 예정된 대규모 리팩토링과 유지보수가 끝난 뒤에 진행한다.** 그때쯤이면 코드가 이동해 있을 것이다. **아래에 적힌 파일 이름·경로·심볼 이름을 맹신하지 말 것.** 모두 v1.15.0 기준이며 바뀌어 있을 가능성이 높다. 구현 전에 현재 구조(SIMD 커널 위치, 엔진/APO/Qt 빌드가 그것을 소비하는 방식, 의존성·업데이트 채널 배선)를 다시 파악할 것.

## 왜 초대공사인가 (난점, 우선순위 순)

### 1. Highway 정적 → 동적 dispatch 전환
각 SIMD TU를 `HWY_DYNAMIC_DISPATCH` 패턴으로 재구성해야 한다. `#define HWY_TARGET_INCLUDE "<이 파일>"` + `#include "hwy/foreach_target.h"`로 TU를 타깃별로 다회 컴파일하고, 커널을 `HWY_NAMESPACE`(+`HWY_ATTR`)로 감싼 뒤 `HWY_EXPORT` + 호출부의 `HWY_DYNAMIC_DISPATCH`로 런타임 함수 포인터 테이블을 만든다. 네 지점(Preamp 게인, convolution 핫패스, BiQuad IIR, float↔double 변환) 각각에 이 래핑이 필요하다. **MSVC 특이점 검증 필수**: MSVC는 함수별 target 속성이 없어 Highway가 TU 다회 컴파일로 처리하는데, 프로젝트의 PCH(`stdafx.h`)·NOMINMAX 가드와 충돌하지 않는지 확인해야 한다.

### 2. FFTW가 진짜 병목 (가장 먼저 풀 것)
FFTW는 별도 선빌드 라이브러리이고 **변형별로 컴파일·배포**된다(`fftw3[avx2]`, `fftw3[sse2]` 등). convolution에서 제일 무거운 건 우리 손커널이 아니라 FFT 자체다. 단일 런타임 dispatch 바이너리라도 **고정 ISA FFTW 하나**가 링크되므로:
- 베이스라인(SSE2) FFTW → AVX-512 CPU가 FFT 가속을 못 받아 dispatch 이득 대부분 상쇄.
- AVX-512 FFTW → 구형 CPU에서 illegal instruction.

진짜 단일 최적 바이너리를 원하면 FFTW도 런타임 선택해야 한다. 평가할 선택지:
- **(a)** ISA별 FFTW DLL을 여럿 번들하고 런타임 CPU 감지로 맞는 것을 `LoadLibrary`. APO(audiodg)도 맞는 DLL을 로드해야 함.
- **(b)** FFTW 자체를 SIMD 런타임 dispatch로 빌드 (MSVC 지원이 빈약 — 조사 필요).
- **(c)** FFTW를 런타임 dispatch FFT(pffft, Highway 기반 FFT 등)로 교체 (초대형, 회귀 참조 대비 전면 수치 재검증 + 라이선스 검토 + 성능 동등성).

이 (a/b/c) 결정 자체가 큰 설계 분기다. **이걸 먼저 못 풀면 B안은 복잡도만 늘고 이득이 거의 없다.**

### 3. CI matrix + 의존성 취득 재작성
build.yml은 6변형을 빌드하고 변형별로 deps(FFTW/libsndfile/muparserx + Highway 헤더)를 받는다. x64를 단일 바이너리로 합치면 matrix(단일 x64 + arm64), 의존성 취득(선택한 FFTW 전략에 따라 어떤 FFTW를 받거나 번들할지), 산출물 이름, 회귀/cross-variant 테스트 배선을 다시 짜야 한다.

### 4. 설치/Velopack 채널 마이그레이션 (사용자 영향)
채널이 변형별이다(`releases.x64-avx2.json` 등). Editor/UpdateChecker 자동 업데이트가 `EAPO_UPDATE_CHANNEL`로 맞는 피드를 받는다. x64를 단일 채널로 합치면 **기존 설치를 이전**해야 한다. 예컨대 x64-avx2 채널 사용자가 자동 업데이트 연속성을 잃지 않고 통합 x64 채널로 넘어가야 한다. Velopack 채널 마이그레이션, `New-ReleaseNotes.ps1`, Editor/UpdateChecker에 박힌 채널 키, `ApoRegistration`이 함께 바뀌어야 한다. **기존 사용자를 조용히 깨뜨릴 위험이 가장 큰 부분**이라 마이그레이션 경로 설계가 핵심이다.

### 5. APO 런타임 제약
APO DLL은 audiodg.exe(제한적·실시간·격리 프로세스)에서 돈다. CPU 감지 + Highway dispatch 테이블 초기화가 거기서 안전해야 한다(RT 경로에서 무거운 초기화 금지, init/LockForProcess에서 1회 감지). audiodg 제약 아래 `SupportedTargets()` 런타임 감지가 안전한지 확인.

### 6. 타깃별 테스트
AudioRegressionTests가 **각 타깃 커널**을 검증해야 하는데, 호스트 x64 러너는 AVX-512/AVX10.1을 실행 못 한다. 정적 변형 빌드 때는 그 타깃을 build-only로 우회했다. 동적 dispatch에선 한 바이너리에 모든 타깃이 들어 있으니, Highway의 타깃 오버라이드(`SetSupportedTargetsForTest`/`HWY_DISABLED_TARGETS`)로 각 타깃을 고정해 −120 dBFS 참조와 비교하도록 설계한다. CI와 AVX-512 실하드웨어 수동 실행이 모든 타깃을 덮도록.

### 7. 바이너리 크기
동적 dispatch는 커널을 타깃 수만큼 컴파일해 넣어 Common/DLL이 커진다. 허용 범위지만 측정해 기록.

## 진행 방식 (메인테이너 지시)
- **multi-agent Workflow로 구현**(Highway 변환 / FFTW 전략 / CI·설치 재작성 / 테스트 하니스로 분해 후 종합).
- PR 후 **적대적 코드 리뷰 / 보안 리뷰**(새 의존성 처리, 런타임 DLL 로딩, 채널 마이그레이션이 보안 민감).
- 리뷰 통과 시에만 머지. 결함 발견 시 **수정은 workflow 없이 수동으로** 하고 재리뷰. 깨끗해질 때까지 반복.

## 현재 접점 (v1.15.0 기준 — 사용 전 재확인, 이동했을 수 있음)
- SIMD 커널: `filters/PreampFilter.cpp`, `filters/BiQuadFilter.cpp(.h)`, `libHybridConv-0.1.1/libHybridConv_eapo.cpp`, `engine/FilterEngine.Process.cpp` (Common.lib로 컴파일 + `Editor.pro`로 Editor에 직접 컴파일).
- Highway 배선: `Common.vcxproj`(HIGHWAY_INCLUDE), `Editor.pro`(HIGHWAY_INCLUDE), `.github/workflows/build.yml`(Download Highway), `setup-build.ps1`.
- FFTW: build.yml 변형별 fetch + `deps/fftw`; libHybridConv·GraphicEQ·Convolution이 사용.
- CI/릴리스: build.yml matrix + create-release(`vpk pack`) + `.github/scripts/New-ReleaseNotes.ps1`, `Bump-Version.ps1`.
- 채널/업데이트: `EAPO_UPDATE_CHANNEL`(Editor.pro/UpdateChecker.pro), Editor/UpdateChecker 업데이트 로직, `services/install/ApoRegistration`, `services/update/VelopackBootstrap`.
- 문서: `docs/SimdBuildMatrix.md`.

## 참고
- A안(Highway 정적 dispatch): PR #43, v1.15.0. A안이 정적 dispatch를 택한 이유가 바로 위 2번 FFTW 제약이다. **2번을 먼저 해결할 것.**
