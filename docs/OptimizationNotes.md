# Optimization Notes

이 문서는 `codex/optimization-pass` 작업에서 확인한 최적화 후보와 처리 결과를 정리합니다.

## 확인 범위

- C/C++ 소스와 헤더 285개, 약 3만 줄을 확인했습니다.
- 주요 대상은 오디오 처리 경로, 필터 구현, VST 처리, 설정 파싱, Qt 도구, 빌드 파일입니다.
- 빌드 파일은 `EqualizerAPO.sln`, 각 `.vcxproj`, Qt `.pro`, GitHub Actions 파일을 기준으로 확인했습니다.

## 완료한 작업

### 오디오 처리 경로

- `FilterEngine::process(float** ...)`에서 매 블록마다 만들던 `std::vector<double*>` 임시 배열을 멤버 버퍼로 옮겼습니다.
- `FilterConfiguration`의 채널별 샘플 버퍼를 채널마다 따로 할당하지 않고 한 번에 할당하도록 바꿨습니다. 설정 로딩 때 할당 횟수가 줄고, 샘플 버퍼가 더 연속적으로 배치됩니다.
- `FilterEngine::addFilters`는 필터 포인터 벡터를 값으로 받지 않고 참조로 받도록 바꿨습니다.

### VST 처리

- `VSTPluginFilter`의 지연 보정에서 매 오디오 블록마다 임시 버퍼를 할당하던 코드를 초기화 시점의 재사용 버퍼로 바꿨습니다.
- VST 지연 보정에서 겹치는 메모리를 `memcpy`로 옮기던 부분을 안전한 순서와 `memmove`로 바꿨습니다.
- VST 플러그인이 입출력 채널 수를 0으로 보고할 때 0으로 나누는 상황을 막았습니다.

### 메모리 해제

- `new[]`로 만든 배열을 `delete`로 해제하던 부분을 `delete[]`로 고쳤습니다.
- 대상 파일은 `AnalysisThread`, `ConvolutionFilter`, `GraphicEQFilter`, `WindowsRegistry`, `WideString`, `VoicemeeterAPOInfo`입니다.

### 설정과 보조 코드

- `ChannelLayout::getChannelNames`에서 결과 벡터 크기를 미리 예약하고, 맵 조회를 한 번만 하도록 바꿨습니다.
- `text::join`은 `wstringstream` 대신 크기를 미리 예약한 `std::wstring`에 붙이도록 바꿨습니다.
- `RegexSearchFunction`은 정규식 매치 결과 벡터 크기를 미리 예약하도록 바꿨습니다.
- `Benchmark`의 임시 경로 버퍼 크기 계산을 문자 배열 기준으로 고쳤습니다.

## 검토했지만 이번 작업에서 바꾸지 않은 항목

- `PreampFilter`와 `BiQuadFilter`에는 이미 SIMD 경로가 있습니다. 더 큰 변경은 수치 결과와 CPU별 분기 확인이 필요해서 이번 작업에서는 건드리지 않았습니다.
- `IIRFilter`는 샘플마다 이전 상태를 참조하므로 큰 폭의 SIMD 변경이 어렵습니다. 상태 이동 방식을 바꾸려면 별도 테스트가 필요합니다.
- `ConvolutionFilter`와 `GraphicEQFilter`는 초기화 비용이 크지만, 실제 블록 처리는 `libHybridConv`에 맡기고 있습니다. 이번 작업에서는 배열 해제 오류만 고쳤습니다.
- Qt GUI 쪽은 사용자 조작 비용이 중심입니다. 오디오 실시간 처리보다 우선순위가 낮아서 안전한 범위의 작은 할당 개선만 반영했습니다.
- CI와 설치 스크립트는 빌드 순서와 산출물 배치가 맞습니다. 동작과 직접 관련 없는 정리는 이번 PR에 넣지 않았습니다.

## 검증 기준

- C++ 프로젝트는 `EqualizerAPO.sln`의 Release 빌드를 우선 확인합니다.
- Qt 도구는 가능한 경우 `Editor`, `DeviceSelector`의 qmake 빌드를 확인합니다.
- 전체 로컬 빌드가 환경 문제로 막히면, 실패 원인과 실행한 명령을 PR에 남깁니다.

## 로컬 검증 결과

2026-05-22에 아래 명령을 실행했습니다.

- TheFireKahuna 쪽 GitHub Actions artifact는 확인 시점에 모두 만료되어 있었습니다.
- 대신 각 저장소의 GitHub Release 자산을 `deps/`에 설치했습니다.
- Visual Studio Build Tools 2026에는 ATL 구성 요소가 없어 `atls.lib` 링크 오류가 났고, `Microsoft.VisualStudio.Component.VC.14.51.ATL`을 추가 설치했습니다.
- Qt 6.10.1 `win64_msvc2022_64`는 공식 Qt 저장소에서 `qtbase`, `qttools`, `qtsvg`, `qttranslations` 패키지를 받아 `Qt/`에 설치했습니다.
- `Common`, `EqualizerAPO`, `Benchmark`, `VoicemeeterClient`는 Release x64 MSBuild 재빌드가 통과했습니다.
- `Editor`, `DeviceSelector`, `UpdateChecker`는 qmake/nmake Release x64 빌드가 통과했습니다.
- 세 Qt 실행 파일은 `windeployqt --release --no-opengl-sw` 배포가 통과했고, `platforms/qwindows.dll`과 SVG 플러그인이 배치됐습니다.
- `Benchmark.exe --help` 실행이 통과해 콘솔 실행 파일의 DLL 로딩을 확인했습니다.
- `EqualizerAPO.sln` 전체 MSBuild는 Qt VS Tools의 `QtMsBuild` 파일이 없어 `DeviceSelector.vcxproj`, `UpdateChecker.vcxproj`에서 실패합니다. CI와 로컬 검증은 이 두 프로젝트를 qmake로 빌드합니다.

## 2026-07-16 멀티코어 및 Qt 재구성 패스

### 적용한 최적화

- `ParallelExecutor`를 설정 준비 전용 Module로 추가했습니다. 호출 스레드도 작업자로 참여하고, 동시성은 하드웨어 스레드 수와 16개 상한 중 작은 값으로 제한합니다. 첫 예외가 발생하면 새 작업을 중단하고 생성한 스레드를 모두 합류한 뒤 원래 예외를 다시 던집니다.
- `ConvolutionFilter`, `GraphicEQFilter`, `MultiConvolutionFilter`의 서로 독립적인 `HConvSingle` 초기화를 병렬화했습니다. FFTW plan 생성은 기존 planner 잠금 아래에 있고, plan 실행과 인스턴스별 버퍼 준비는 병렬로 진행됩니다.
- 큰 다채널 IR의 planar 변환을 채널 단위로 병렬화했습니다. 작은 IR은 스레드 생성 비용이 더 크므로 기존 순차 루프를 유지합니다.
- `HConvSingleArray`는 완료된 prefix만 추적하던 소유권 모델을 없앴습니다. 모든 C 슬롯을 먼저 영 상태로 만들고 전체 capacity를 닫으므로 병렬 작업이 어떤 순서로 끝나거나 중간 실패해도 정리가 안전합니다.
- Qt modern-card 재구성은 `FilterCardBuildPlan` Seam에서 descriptor와 scope를 한 번만 준비합니다. 행 GUI 선택, 카드 생성, 스킨 정보가 같은 결과를 공유하므로 한 줄당 반복되던 정규식/inline-expression 해석이 사라집니다.
- 고정 `QRegularExpression`은 재사용하고, 타입 배지 SVG tint 결과는 resource/color/size/DPR 키로 `QPixmapCache`에 보관합니다. 동일한 채널 배지 목록도 다시 만들지 않습니다.

### 의도적으로 제외한 항목

- (2026-11-19에 Convolution과 MultiConvolution에 한해 뒤집었습니다. 아래 '2026-11-19 실시간 컨볼루션 병렬 처리' 절을 보십시오.) 오디오 callback 안에 영구 worker pool을 두지 않았습니다. endpoint마다 별도 callback이 실행되는 환경에서 일반 작업자 스레드를 기다리면 MMCSS 우선순위 역전과 다중 endpoint 경합이 생길 수 있습니다. 현재 AVRT 경로에는 새 할당, mutex, 예외, 로그가 없습니다.
- 필터 체인 전체 병렬화는 적용하지 않았습니다. 필터가 앞 단계의 채널/버퍼 출력을 다음 단계 입력으로 소비하므로 일반적인 병렬 실행은 DSP 순서와 결과를 바꿉니다.
- `MultiConvolution` 합산 루프의 SIMD 변경은 이번 패스에서 제외했습니다. 곱셈-덧셈 결합 여부가 마지막 비트를 바꿀 수 있어, 별도 수치 계약과 기준 데이터 갱신 없이 적용하지 않습니다.
- Qt 카드 widget virtualization은 장기 후보로 남겼습니다. 현재 스킨 Interface가 각 행의 실제 QWidget과 body editor를 구성하므로, viewport 재활용은 별도 상태 모델과 포커스/접근성 설계가 필요한 독립 프로젝트입니다.

### CI 회귀 경계

- `EngineOrchestrationTests`가 병렬 작업의 exactly-once 실행과 예외 합류/전파를 검사합니다.
- `HybridConvTests`가 뒤쪽 슬롯을 먼저 초기화한 배열도 반복 정리 가능한지 검사합니다.
- `EditorLogicTests`가 build plan의 descriptor/scope/dynamic-line 결과를 기존 계산과 대조합니다.
- offscreen skin-switch test의 재구성 상한은 코드 기본 8초이고, CI는 `EAPO_SWITCH_LIMIT_MS=5000`/`EAPO_SWITCH_WARN_MS=2500`으로 조입니다(build.yml). 공유 러너의 속도 편차 때문에 한계는 환경 변수로 조정 가능하게 두었고, 로컬 장시간 검증을 반복하지 않고 PR의 AVX2 CI를 성능·수명 회귀 판정 기준으로 사용합니다. (감사 #250 F051: 이전 서술의 "4초"는 실제 CI 값과 어긋난 낡은 기록이었습니다.)

## 2026-11-19 실시간 컨볼루션 병렬 처리

메인테이너의 결정으로, 7월에 제외했던 '오디오 callback 안의 worker'를 Convolution과 MultiConvolution의 컨볼버 유닛에 한해 들였습니다. 메인테이너의 실제 설정(업믹서 VST 뒤에 2초 BRIR 22유닛)에서 블록 시간의 절반이 서로 독립인 유닛을 차례로 도는 데 쓰였기 때문입니다.

### 적용한 것

- `runtime/concurrency/RealtimeFanOut`(`eapo::rt::FanOut`)이 프로세스에 하나뿐인 작업자 팀을 관리합니다. 작업자는 최대 7개이고 MMCSS 'Pro Audio'에 들어가며, 일이 없을 때는 이벤트에서 잡니다. 팀은 필터가 쥔 Lease가 처음 생길 때 시작하고 마지막 Lease가 풀릴 때 멈춥니다. 정적 소멸이나 DllMain에서는 건드리지 않습니다.
- `FanOut::run`은 할당, 대기 잠금, 예외, 로그 없이 작업을 정확히 한 번씩 실행합니다. 호출한 오디오 스레드도 작업을 가져갑니다. 작업자가 제때 깨지 않으면 호출자가 전부 처리합니다. 다른 엔드포인트가 팀을 쓰고 있으면 기다리지 않고 혼자 처리합니다.
- 늦게 깬 작업자는 끝난 작업의 context에 닿지 못합니다. epoch, 열림 비트, 참여 수를 원자 값 하나에 담아, 호출자가 닫은 뒤에는 참여 CAS가 실패합니다.
- 작업자는 작업마다 호출자의 FP 제어 상태(FTZ/DAZ)를 그대로 받습니다. MultiConvolution은 유닛마다 따로 계산하고 합산은 전과 같은 순서로 오디오 스레드가 합니다. 그래서 출력은 순차 실행과 비트 단위로 같고, 테스트가 `memcmp`로 확인합니다.
- 작은 작업은 나누지 않습니다. `유닛 수 x 블록 x 파티션 수`가 24576 미만이면 순차로 돕니다. 근거가 된 측정은 `filters/ConvolverProcess.h`의 주석에 있습니다.
- 끄는 방법은 Device Selector의 문제 해결 옵션 '멀티코어 컨볼루션 처리'(레지스트리 `ParallelConvolution`, 기본값 켜짐)입니다.

### 7월의 우려에 대한 답

- 우선순위 역전: 작업자를 MMCSS로 올리고, 호출자의 스핀은 PAUSE 64회로 제한한 뒤 이벤트로 기다립니다(`asio/EngineHostCore.cpp`가 기록한 MMCSS 스핀 문제를 따랐습니다).
- 다중 endpoint 경합: 작업 슬롯이 하나이고, 못 잡은 쪽은 순차로 돕니다.
- 남은 한계: 이미 작업을 가져간 작업자를 Windows가 늦게 돌리면 호출자는 그 작업이 끝나기를 기다립니다. 최악 시간이 순차 실행보다 나빠지지 않는다고 보장하지는 못합니다.

### 측정 (이 PC, 실제 설정, 48 kHz 스테레오 30초, 변경 후는 세 번 중 중앙값)

| 블록 | 블록 시간 중앙값 전 → 후 (us) | p99 전 → 후 (us) | MultiConvolution 평균 전 → 후 (us) |
| --: | --: | --: | --: |
| 480 | 1433 → 1033 | 2996 → 2103 | 814 → 393 |
| 128 | 481 → 290 | 1583 → 1150 | 333 → 137 |
| 64 | 214 → 105 | 1170 → 906 | 203 → 81 |

최대값은 실행마다 크게 달라(480에서 2582~4173 us, 변경 전 3916 us) 개선을 확인하지 못했습니다. 실제 audiodg 안에서의 동작과 여러 endpoint가 동시에 도는 경우는 측정하지 않았습니다.

## 2026-07-16 공유 convolution 필터뱅크

- `HConvSingle`을 불변 필터뱅크와 채널별 런타임 상태로 분리했습니다. 필터뱅크는 주파수 영역 IR 파티션만 소유하며 `shared_ptr<const ...>`로 수명이 관리됩니다. history, mix slab, DFT 작업 버퍼와 FFTW plan은 계속 각 채널이 따로 소유합니다.
- GraphicEQ는 합성 IR을 한 번만 변환합니다. Convolution은 서로 다른 IR 채널마다 한 번, MultiConvolution은 실제로 참조한 IR 채널마다 한 번만 변환하며, 같은 IR을 쓰는 나머지 출력은 완성된 뱅크를 공유합니다.
- 공개 `HConvSingle` Interface의 필터 파티션 포인터를 `const`로 바꿔 오디오 처리 경로가 공유 뱅크를 수정할 수 없게 했습니다. AVRT 경로의 연산 순서와 채널별 상태는 바뀌지 않았습니다.
- `HybridConvTests`는 형제 인스턴스가 동일한 필터뱅크와 서로 다른 가변 버퍼를 갖는지, 원본 인스턴스를 먼저 닫아도 공유 소유권으로 convolution 출력이 유지되는지 검사합니다.
