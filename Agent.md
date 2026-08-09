# Agent Guide

이 문서는 이 저장소에서 Codex가 작업할 때 따라야 할 기준을 정리합니다. 코드 변경, 문서 작성, README 같은 최종사용자 대상 글을 작성할 때 함께 적용합니다.

## 프로젝트 개요

EqualizerAPO-XT는 Windows용 시스템 전체 이퀄라이저인 Equalizer APO 1.4.2를 바탕으로 한 포크입니다. 주요 변경점은 64비트 double 기반 오디오 처리, AVX2/AVX-512/AVX10.1 최적화, ARM64 빌드, Visual Studio 2022/2026과 GitHub Actions를 이용한 자동 빌드입니다.

핵심 출력물은 Windows APO DLL, Qt 기반 GUI 도구, 보조 실행 파일, Velopack 설치 파일입니다. 외부 의존성은 FFTW, libsndfile, muparserx, TCLAP, Qt입니다.

## 작업 우선순위와 TODO

- [x] 작업을 시작하기 전에, 대규모 리팩토링을 합니다. 원본 및 포크 작업자가 어련히 잘 해줬을 것이라는 환상은 신뢰하지 않으며, 본격적인 빌드를 위한 기초를 다지는 작업입니다.
- [x] 리버브 재생이 약 1000ms 지점에서 사라지는 문제를 먼저 재현합니다. 관련 코드는 `filters/`, `libHybridConv-0.1.1/`, 버퍼 수명, frame count 계산, delay/convolution 처리 경로를 함께 확인합니다.
- [x] 문제를 고친 뒤 재발을 막을 수 있는 검증 방법을 남깁니다. 자동 테스트가 어렵다면 최소 재현 설정, 벤치마크 실행 방법, 수동 확인 절차를 문서로 남깁니다.
- [x] Editor와 보조 GUI의 사용 흐름을 정리하고 낡은 UI를 단계적으로 고칩니다. 필터 편집, 파일 선택, 오류 표시, convolution 관련 화면을 우선 확인합니다.
- [x] convolution 기능을 확장합니다. 런타임 처리, 설정 파싱, GUI, 문서, 설치 산출물이 같은 기능 범위를 지원해야 합니다.
- [x] AVX2/AVX-512 중심의 포크 변경을 AVX10 방향으로 정리합니다. AVX를 지원하지 않는 장치와 ARM64 경로도 함께 고려합니다.
- [x] SIMD 변경은 빌드 플래그, 런타임 CPU 기능 확인, 외부 의존성 변형, CI matrix, 설치 파일 이름이 서로 맞는지 확인합니다.
- [x] Velopack으로 감싼 이유는 추후 자동 업데이트 기능을 추가하기 위함입니다. Velopack과 부드럽게 연동되는 자동 업데이트 기능을 추가합니다.

## 저장소 구조

- `EqualizerAPO.sln`: Visual Studio 솔루션입니다. `Common`, `EqualizerAPO`, `SubwooferRoutingCore`, `SubwooferRoutingVst3`, `Benchmark`, `VoicemeeterClient`, `DeviceSelector`, `UpdateChecker`, `Installer`, `TestVst2Plugin`, `TestVst3Plugin`, `HybridConvTests`, `EditorLogicTests`, `EngineOrchestrationTests`, `AudioRegressionTests` 프로젝트를 묶습니다.
- `Common.vcxproj`: 필터 엔진, 필터 구현, 파서 확장, 도메인별 공용 모듈을 포함하는 정적 라이브러리입니다.
- `EqualizerAPO/`: Windows Audio Processing Object DLL 프로젝트입니다. ATL 기반이므로 `atls.lib`가 필요합니다.
- `Editor/`: Qt 기반 설정 편집기입니다. `.pro`, `.ui`, 리소스, 번역 파일, 필터별 GUI가 있습니다.
- `DeviceSelector/`: Qt 기반 장치 선택 도구입니다.
- `UpdateChecker/`: Qt 기반 업데이트 확인 도구입니다.
- `Benchmark/`: 오디오 처리 성능 측정용 콘솔 프로그램입니다.
- `VoicemeeterClient/`: Voicemeeter 연동용 보조 프로그램입니다.
- `filters/`: 실제 오디오 필터 구현과 각 필터의 factory가 있습니다. 새 필터는 구현 파일, 헤더, factory, 필요하면 GUI를 함께 봅니다.
- `parser/`: muparserx에 붙는 논리 연산자, 문자열 함수, 정규식 함수, 레지스트리 함수입니다.
- `audio/`, `dsp/`, `platform/`, `runtime/`, `services/`, `text/`, `vst/`: 오디오 지식, 자원 수명, Windows 어댑터, 서비스, VST 호스트 같은 공용 모듈을 책임별로 나눕니다. 범용 `helpers/` 폴더는 사용하지 않습니다.
- `libHybridConv-0.1.1/`: convolution 처리에 쓰는 libHybridConv 코드와 Equalizer APO 연결 코드입니다.
- `Tests/`: `HybridConvTests`, `EditorLogicTests`, `AudioRegressionTests` 등 단위/회귀 테스트 프로젝트가 있습니다.
- `Setup/`: 설치 시 함께 들어가는 기본 설정 파일입니다.
- `.github/workflows/build.yml`: CI 빌드와 설치 파일 생성 파이프라인입니다.
- `.github/simd-variants.psd1`: SIMD 변형 매트릭스와 의존성 핀의 단일 기준 파일입니다.
- `version.h`: 릴리스 버전의 기준 파일입니다.
- `setup-build.ps1`: 로컬 빌드 의존성(deps/, Qt) 설치 스크립트입니다.
- `uncrustify.cfg`, `reformat.bat`: C/C++ 코드 포맷 기준과 실행 스크립트입니다.
- `Wiki/`: 기존 사용자 문서 자료입니다.

## 빌드와 검증

- C++ 프로젝트는 Visual Studio 2022/2026 계열 도구와 Windows SDK 10.0을 기준으로 합니다. 현재 로컬 프로젝트는 VS 2026 `v145`에서 빌드하며, VS 2022만 있는 환경에서는 `/p:PlatformToolset=v143`으로 덮어쓰면 됩니다. CI는 x64에서 `v145`, ARM64 runner에서 `v143`을 씁니다.
- `.vcxproj`는 C++17을 사용합니다. 기존 `UNICODE`, `_UNICODE`, `MUP_USE_WIDE_STRING` 정의를 유지합니다.
- Qt 도구는 `Editor`, `DeviceSelector`, `UpdateChecker`에서 `.pro` 파일을 중심으로 관리합니다.
- 로컬 빌드 준비는 `setup-build.ps1`을 기준으로 봅니다. 이 스크립트는 `deps/` 아래 외부 라이브러리와 Qt 6.10.1을 설치합니다. 빌드는 MSBuild(`EqualizerAPO.sln`의 vcxproj들)와 qmake/nmake(Qt 도구)로 나뉩니다.
- CI는 x64 `sse2`, `avx`, `avx2`, `avx512`, `avx10_1`, ARM64 `neon` 조합을 빌드하고 산출물과 설치 파일을 업로드합니다. 변형 목록과 의존성 핀은 `.github/simd-variants.psd1`이 기준입니다.
- `main`에 push되면 CI가 모든 변형 빌드를 끝낸 뒤 GitHub Release를 만듭니다. Release에는 Velopack으로 감싼 채널별 설치 파일, CPU 자동 감지 설치기(`EqualizerAPO-XT-Setup.exe`), `git archive`로 만든 소스 코드 zip이 올라갑니다.
- APO 설치와 등록은 Editor가 처리하는 Velopack 훅(`services/install/ApoRegistration`, `services/update/VelopackBootstrap`, `Editor/main.cpp`)이 담당합니다. NSIS 기반 설치는 제거되었습니다.
- 외부 라이브러리 경로는 프로젝트 파일의 환경 변수 기본값과 CI의 `deps` 경로를 함께 확인합니다. 주요 변수는 `FFTW_INCLUDE`, `FFTW_LIB`, `LIBSNDFILE_INCLUDE`, `LIBSNDFILE_LIB`, `MUPARSERX_INCLUDE`, `MUPARSERX_LIB`, `TCLAP_ROOT`입니다.
- 로컬 의존성 설치와 검증 결과는 `docs/LocalDependencySetup.md`와 `docs/OptimizationNotes.md`를 함께 봅니다. GitHub Actions artifact는 만료될 수 있으므로, Release 자산과 qmake 빌드 경로도 확인합니다.
- 테스트 실행 파일은 빌드 산출물과 같은 디렉터리에 만들어집니다. `EditorLogicTests.exe`, `HybridConvTests.exe`, `AudioRegressionTests.exe`를 실행할 때는 FFTW와 libsndfile DLL이 `PATH`에 있어야 합니다. 작은 변경은 관련 프로젝트 빌드로 확인하고, 공용 엔진이나 설치 파일에 영향을 주는 변경은 가능한 경우 CI와 같은 범위의 빌드를 확인합니다.

## CI 감시와 회귀 테스트

- `main`에 push하거나 CI 설정을 바꾼 뒤에는 GitHub Actions 실행이 끝날 때까지 감시합니다. 실패하면 실패한 job 로그를 읽고, 원인을 고친 뒤 새 커밋으로 push하고 다시 감시합니다.
- CI annotation도 작업 범위입니다. warning이나 notice가 다음 릴리스에서 사용자에게 혼란을 줄 수 있거나 곧 실패로 바뀔 예정이면 방치하지 않습니다.
- 테스트를 건너뛰는 판단은 명시적인 이유가 있어야 합니다. 예를 들어 GitHub-hosted x64 runner가 AVX-512/AVX10.1 실행을 보장하지 않아 `HybridConvTests` 실행을 건너뛰는 경우처럼, 빌드는 유지하되 런타임 실행만 제한합니다.
- 회귀가 발견되면 먼저 재현 가능한 테스트나 검증 명령을 추가합니다. 오디오 처리 내부처럼 버그를 잡기 어려운 부분은 더 좁은 테스트를 먼저 만들고, 테스트가 가리키는 범위만 수정합니다.
- 공용 엔진, 설정 파싱, 업데이트 확인, 설치/릴리스 파이프라인을 바꿀 때는 관련 테스트를 함께 돌립니다. 최소 기준은 `git diff --check`, 관련 로컬 테스트 실행, 그리고 push 후 CI 전체 성공 확인입니다.
- CI가 만드는 GitHub Release 본문은 `.github/scripts/New-ReleaseNotes.ps1`가 생성합니다. 릴리스 노트에는 각 파일의 용도, 일반 사용자가 받을 설치 파일, 이전 릴리스 대비 변경 사항 링크, CI 실행 링크가 들어가야 합니다.

## 버전 변경 기준

- 릴리스 기본 버전은 `version.h`의 `MAJOR`, `MINOR`, `REVISION`입니다. `main`에 push되면 CI의 version-bump job(`.github/scripts/Bump-Version.ps1`)이 커밋 메시지의 Conventional Commits 타입을 읽어 `version.h`를 자동으로 올리고, 깨끗한 `vX.Y.Z` 태그로 릴리스를 만듭니다.
- `version.h`는 손으로 올리지 않습니다. 커밋 타입이 버전을 결정합니다.
- 문서 수정, CI 경고 수정, 릴리스 노트 개선, 빌드 안정화처럼 사용자 설치 결과나 공개 API가 바뀌지 않는 작업은 `docs:`, `ci:`, `chore:` 등으로 커밋해 버전을 올리지 않습니다.
- 버그 수정, 작은 동작 수정, 호환성을 유지하는 설치/업데이트 수정은 `fix:`로 커밋해 `REVISION`을 올립니다.
- 새 기능, 새 필터, 새 SIMD/아키텍처 릴리스 채널, 사용자에게 보이는 동작 변경은 `feat:`로 커밋해 `MINOR`를 올립니다.
- 설정 파일 형식, 설치 방식, 업데이트 채널, 공개 동작이 기존 사용자에게 수동 조치를 요구할 만큼 바뀌면 BREAKING CHANGE로 표시해 `MAJOR`를 올립니다.

## 코드 작업 기준

- 먼저 관련 `.vcxproj`, `.pro`, 소스 파일을 읽고 기존 방식에 맞춥니다.
- 공용 오디오 처리 변경은 `FilterEngine`, `FilterConfiguration`, `IFilter`, `filters/`, `audio/`, `dsp/`, `runtime/`의 영향 범위를 같이 확인합니다.
- 필터를 추가하거나 바꿀 때는 런타임 구현, factory, 설정 파싱, Editor GUI, 리소스 파일, 프로젝트 파일 포함 여부를 함께 확인합니다.
- GUI 변경은 Qt `.ui`, `.qrc`, `.pro`, 번역 파일 영향을 확인합니다. 번역 파일은 요청이나 실제 문자열 변경이 있을 때만 건드립니다.
- 설치 관련 변경은 CI `create-release` job의 `vpk pack` 호출과 `services/install/ApoRegistration`, `services/update/VelopackBootstrap`, `Editor/main.cpp`의 Velopack 훅 처리를 함께 확인합니다. NSIS 기반 설치는 제거되었습니다.
- 릴리스 버전 변경은 `version.h`를 기준으로 합니다.
- 외부 의존성을 저장소에 새로 넣지 않습니다. 이미 있는 vendored 코드나 라이선스 파일은 필요한 범위에서만 수정합니다.
- 빌드 산출물, Qt 배포 DLL, 임시 디렉터리, IDE 로컬 설정은 커밋하지 않습니다.
- 포맷은 기존 C++/Qt 스타일과 `uncrustify.cfg`를 따릅니다. 큰 파일 전체 포맷 변경은 요청이 있을 때만 합니다.

## 문서 작성 기준

- README, 릴리스 노트, 사용자 안내 문서는 사용자가 바로 실행할 수 있는 정보부터 씁니다.
- 지원 아키텍처, SIMD 변형, 필요 도구, 의존성 경로, 설치 파일 이름처럼 빌드와 설치에 영향을 주는 이름은 실제 파일명과 맞춥니다.
- 추측한 내용은 사실처럼 쓰지 않습니다. 확인하지 못한 부분은 확인하지 못했다고 씁니다.
- 기존 영어 문서를 고칠 때도 문장을 짧게 쓰고, 과장된 홍보 문구보다 정확한 설명을 우선합니다.
- README와 CHANGELOG는 영어판과 한국어판(README.md/README.ko.md, CHANGELOG.md/CHANGELOG.ko.md) 네 파일을 항상 함께 갱신합니다. 사용자에게 보이는 변경이 main에 들어가면 CHANGELOG의 Unreleased 절에 PR 링크와 함께 기록하고, 릴리스가 만들어지면 Unreleased 절을 해당 버전 절로 옮깁니다. README의 '지금 진행 중인 작업' 목록은 작업이 시작되거나 끝날 때 같은 PR 또는 직후의 docs PR로 갱신합니다.

## 한국어 응답과 최종사용자 대상 글 기준

- 한국어 답변은 존댓말로 씁니다.
- 한국어 화자가 실제로 고를 법한 말로 짧고 직설적으로 씁니다.
- 영어식 번역문처럼 쓰지 않습니다.
- 영어식 완충 표현을 한국어의 정도 표현으로 옮기지 않습니다.
- 사용자의 말을 채점하는 표현을 피합니다. 예를 들어 `상당히 맞습니다`, `절반은 맞습니다`, `부분적으로 타당합니다`, `완전히 틀린 것은 아닙니다`라고 쓰지 않습니다.
- 맞으면 `맞습니다`, 틀리면 `그 부분은 틀렸습니다`라고 씁니다. 일부만 맞으면 어디가 맞고 어디가 다른지 바로 말합니다.
- `조금`, `꽤`, `상당히`, `대체로` 같은 한정사는 꼭 필요할 때만 씁니다.
- `냄새`, `층위`, `겹`, `구조`, `작동 방식`, `함의` 같은 비유와 추상어를 습관처럼 쓰지 않습니다.
- `번역 냄새` 대신 `번역문 같습니다`, `여러 층위` 대신 `이유가 여럿 있습니다`처럼 씁니다.
- 한국어에서 물리적 뜻이 먼저 떠오르는 동사를 조심합니다.
- 말을 `받다`라고 하지 말고 `받아들이다`, `이해하다`, `알아듣다`를 씁니다.
- 의미가 `간다`라고 하지 말고 `그런 말이 된다`, `그렇게 이해한다`를 씁니다.
- 의견은 `다르게 봅니다`보다 `다르게 생각합니다`라고 씁니다.
- `회피로 들립니다`처럼 명사를 `듣다`의 대상으로 삼지 않습니다. `사용자가 그렇게 받아들인다`처럼 주체를 밝힙니다.
- 물리적 의미로 쓰이는 형용사를 조심합니다. `선명하다`는 직접 시야에 보이는 대상에게만 씁니다.
- `처럼 보입니다`, `읽힙니다`를 남용하지 않습니다. `같습니다`, `그렇게 느낄 수 있습니다`, `그렇게 이해할 수 있습니다`를 씁니다.
- 수동 표현은 필요할 때만 씁니다.
- `말이 안 붙습니다`, `선명하게 정리하면`, `이상한 그림이 생깁니다` 같은 어색한 비유를 피합니다.
- `문장:`이나 `고치면:`처럼 영어식으로 쌍점을 붙이지 않습니다. 문장으로 풀어 씁니다.
- 답변을 마친 뒤, 한국어 문장이 엉뚱한 물리적 장면을 떠올리게 하지 않는지 확인합니다.
