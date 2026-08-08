# EqualizerAPO-XT

[English](README.md) | **한국어**

EqualizerAPO-XT는 Windows용 시스템 전체 이퀄라이저인 [Equalizer APO 1.4.2](https://sourceforge.net/p/equalizerapo/)를 바탕으로 활발히 개발 중인 포크입니다. Equalizer APO의 시스템 전체 오디오 처리 모델은 그대로 두고, 오디오 엔진과 빌드 파이프라인, GUI 도구를 현대화했습니다.

이 포크는 [equalizer-apo-64](https://github.com/chebum/equalizer-apo-64)의 double 정밀도 작업과, 그 뒤를 이은 TheFireKahuna의 Equalizer APO 포크들의 SIMD/빌드 작업을 이어받았습니다.

설정 방법이 궁금하다면 [GitHub Wiki](https://github.com/115dkk/EqualizerAPO-XT/wiki)를 보십시오. 설치 안내와 모든 필터 명령을 다루는 [설정 레퍼런스](https://github.com/115dkk/EqualizerAPO-XT/wiki/Korean-Configuration-reference)가 영어판과 한국어판으로 있습니다.

## 프로젝트 현황

포크가 처음 목표로 잡았던 작업은 모두 끝났습니다. 컨볼루션 꼬리가 끊기던 버그를 고쳤고, 엔진 코드를 리팩토링해 회귀 테스트로 보호했으며, Editor UI를 새로 만들었고, SIMD 지원을 정리했습니다. 릴리스는 자동 업데이트가 되는 Velopack 패키지로 나갑니다. 포크 이후의 전체 이력은 [CHANGELOG.ko.md](CHANGELOG.ko.md)에 있습니다.

지금 진행 중인 작업은 다음과 같습니다.

1. 변형별 릴리스 채널을 단일 바이너리 런타임 SIMD dispatch로 대체합니다([docs/RuntimeDispatchEpic.md](docs/RuntimeDispatchEpic.md)).
2. 격주 자동 코드 감사가 찾아낸 문제를 처리합니다.
3. 커뮤니티 피드백 라운드를 반영해 Editor 스킨을 다듬습니다(1차: Soft 파스텔 재작업, 다크 모드 상태 대비, 분석 패널 한칸 배치. 3차: 그래픽 EQ 모던 카드, 카드 추가·삽입 계약, Device Selector 스킨 동기화, [#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172)).
4. 위상과 시간. 분석 그래프가 크기·위상·그룹 지연을 전환하고, 올패스 필터에 전용 카드와 1차 섹션이 생겼으며, `Delay`와 올패스가 픽커의 'Phase & Time' 분류로 묶였습니다. 올패스는 음량을 전혀 바꾸지 않으므로 크기만 그리는 그래프로는 볼 수 없는 필터였습니다([#228](https://github.com/115dkk/EqualizerAPO-XT/issues/228), [docs/features/phase-and-time.md](docs/features/phase-and-time.md)).
5. 프로그래밍 계열 설정 명령(`If:`/`ElseIf:`/`Else:`/`EndIf:`/`Eval:`) 전용 에디터 — 완료. 다섯 스킨이 분석 판정으로 블록을 각자의 계기로 표현하고, 픽커가 이 명령들을 삽입하며, 계수 직접 입력 IIR 줄과 백틱 인라인 식이 든 줄도 각자의 카드를 유지합니다([#178](https://github.com/115dkk/EqualizerAPO-XT/pull/178), [#182](https://github.com/115dkk/EqualizerAPO-XT/pull/182), [#183](https://github.com/115dkk/EqualizerAPO-XT/pull/183), [#184](https://github.com/115dkk/EqualizerAPO-XT/pull/184)).
6. 서브우퍼 라우팅([#246](https://github.com/115dkk/EqualizerAPO-XT/issues/246)) — 핵심 기능은 완료됐습니다. `SubwooferRouting:` 명령, MIT SubwooferRoutingCore DSP 라이브러리, 독립 실행형 VST3 플러그인, 4.1 호스트 협상 수정, 다섯 스킨 각각의 카드 계기, 그리고 두 라우팅 행렬과 응답 뷰를 갖춘 전체 편집기까지 들어갔습니다. 남은 후속 작업은 연결된 프로필 파일로의 변경 사항 되쓰기(현재는 행을 인라인 상태로 전환), audition/solo 오버라이드, 새 문자열의 한국어 번역, 전용 VST3 편집기 화면(현재는 호스트의 일반 파라미터 화면)입니다.

## 주요 기능

- 오디오를 내부에서 double 정밀도로 처리해 복잡한 필터 체인에서도 정밀도를 잃지 않습니다.
- Convolution, GraphicEQ, 파라메트릭 EQ, VST2/VST3와 기존 Equalizer APO 필터를 지원합니다.
- 트루 스테레오와 BRIR(Binaural Room Impulse Response) 재생을 위한 MultiConvolution 필터가 있습니다. `MultiConvolution: L=0+1 R=2+3 brir.wav`처럼 각 채널 자신의 신호를 매핑된 파일 채널들과 컨볼루션해 합산하며, Channel 명령과 무관하게 동작합니다. 제자리 처리만 하는 기존 Convolution 필터가 표현하지 못하는 분기·합산 패턴이 한 줄로 끝나고, 파일 채널마다 Copy와 같은 문법으로 배율을 붙일 수 있습니다(`L=0.5*0+1`, `-1`은 역위상, `-6dB`도 가능). Editor에서는 스킨마다 다른 라우팅 화면으로 매핑을 편집합니다.
- 1025탭 선형 위상 힐베르트 변환이 내장되어 있습니다.
  `Hilbert: Shift=SL,SR Align=L,R Direction=-90`처럼 위상을 ±90° 바꿀
  채널과 512샘플 지연만 맞출 채널을 명시적으로 나눕니다.
- 희소 벨벳 노이즈 비상관화 필터가 내장되어 있습니다.
  `Velvet: Mode=Dynamic`은 채널마다 독립적인 단위 에너지 커널을 만들고 동일
  전력 전환으로 커널을 계속 갱신합니다. 고정 모드와 양·시간 분산·밀도·변화
  주기·전환·감쇠·결정적 변형을 모두 직접 설정할 수 있습니다. 시간에 따라
  변하는 응답에는 영구적인 곡선 하나가 없으므로, 주파수 응답 분석은 동적
  모드를 결정적 커널 하나로 고정하고 그래프에 스냅샷임을 표시합니다. 같은
  휴대용 DSP를 EqualizerAPO-XT 밖에서 쓰는 독립 MIT 프로젝트
  [Dynamic Velvet Decorrelator VST3](https://github.com/115dkk/Velvet-Noise-Decorrelator-VST3)도
  별도로 배포합니다.
- 한 줄 서브우퍼 라우팅: `SubwooferRouting:`가 스피커 그룹별 크로스오버, 전용 베이스 경로, 물리 LFE 입력 보존, 경로별 게인·극성·지연·EQ, 출력 합산 행렬을 JSON 상태 하나로(인라인 또는 `*.swxt.json` 프로필) 실행합니다. 자동 헤드룸이 합산 출력을 지키고, 내장 프리셋은 이슈 #246의 원본 사슬을 샘플 단위로 재현합니다. 같은 MIT 라이선스 DSP 코어가 독립 실행형 `EAPO XT Subwoofer Routing` VST3 플러그인으로도 실려, 동일한 JSON 상태를 주고받습니다.
- Steinberg VST3 SDK(MIT 라이선스 pluginterfaces)로 VST3를 네이티브 호스팅하며, 플러그인이 지원하면 64비트(double)로 처리합니다. 채널 배치는 실제 채널 이름으로 협상하므로 4.1 시스템이 5.0으로 잘못 알려지지 않습니다.
- SIMD 커널은 [Google Highway](https://github.com/google/highway)로 한 번만 작성해 변형별로 컴파일합니다. x64는 SSE2, AVX, AVX2, AVX-512, AVX10.1, ARM64는 NEON입니다.
- Qt Editor를 현대화했습니다. 카드 기반 필터 UI와 행 chrome·노브 렌더링·Copy 라우팅 렌더러까지 서로 다른 5종 스킨([docs/skin-integration-report.md](docs/skin-integration-report.md)), 내장 폰트, 고해상도(High-DPI) 대응이 들어 있습니다.
- Editor가 새 릴리스를 백그라운드에서 내려받아 종료할 때 적용하는 자동 업데이트가 들어 있습니다. 알림만 하는 UpdateChecker 도구도 따로 있습니다.
- 자동 감지 설치기가 로컬 CPU에 맞는 SIMD 빌드를 골라 내려받고, 실행 전에 릴리스 체크섬으로 검증합니다.
- 오디오 처리는 AOCL-FFTW, libsndfile, muparserx, TCLAP을 쓰고, GUI 도구는 Qt로 만들었습니다.
- Windows 호환성을 위해 공유 VC++ 런타임 DLL을 함께 배포합니다.
- GitHub Actions 파이프라인이 빌드, 테스트, 설치 파일, 릴리스를 만들고, 격주 자동 코드 감사가 트리를 직접 빌드해 테스트까지 돌립니다.

## 설치

[Releases 페이지](https://github.com/115dkk/EqualizerAPO-XT/releases)에서 설치합니다. `main`에 push되면 CI가 지원하는 모든 변형을 빌드하고, Velopack으로 감싼 설치 파일과 소스 코드 zip을 담은 GitHub Release를 만듭니다.

권장 다운로드는 자동 감지 설치기인 **EqualizerAPO-XT-Setup.exe**입니다. CPU(아키텍처와 AVX 수준)를 감지해 맞는 빌드를 내려받으므로 SIMD 변형을 직접 고를 필요가 없으며, 내려받은 파일은 실행 전에 릴리스의 `SHA256SUMS.txt`로 검증합니다. 특정 빌드를 골라 설치하고 싶다면 채널별 `…-Setup.exe` 파일도 그대로 받을 수 있습니다. [docs/AutoDetectInstaller.md](docs/AutoDetectInstaller.md)를 참고하십시오.

설치 후에는 Editor가 스스로 최신 상태를 유지합니다. 새 릴리스를 백그라운드에서 내려받아 두었다가 Editor를 닫을 때 적용합니다. 자세한 흐름은 [docs/VelopackUpdates.md](docs/VelopackUpdates.md)에 있습니다.

## 문서

사용자 문서는 [GitHub Wiki](https://github.com/115dkk/EqualizerAPO-XT/wiki)에 영어판과 한국어판이 있으며, 이 저장소의 `Wiki/github-wiki/`에서 동기화됩니다. 개발자 문서는 [docs/](docs/) 아래에 있습니다(영어).

## 빌드

프로젝트는 Visual Studio, Qt, Velopack과 몇 가지 외부 라이브러리를 사용합니다. [setup-build.ps1](setup-build.ps1)을 실행하면 로컬 빌드에 필요한 것(바이너리 의존성, 헤더 전용 체크아웃, Qt 6.10.1)이 모두 준비됩니다. 수동 배치 방법은 [docs/LocalDependencySetup.md](docs/LocalDependencySetup.md)에 있습니다.

포크된 의존성 저장소는 다음과 같습니다.

- [AOCL-FFTW 5.1 / FFTW 3.3.10](https://github.com/thefirekahuna/amd-fftw)
- [muparserx 4.0.13](https://github.com/thefirekahuna/muparserx)
- [libsndfile 1.2.2](https://github.com/thefirekahuna/libsndfile)
- [tclap 1.2.5](https://github.com/thefirekahuna/tclap)

헤더 전용 의존성 두 개는 저장소에 넣지 않고 체크아웃합니다. [Google Highway](https://github.com/google/highway)는 `deps/highway`에, Steinberg [VST3 pluginterfaces](https://github.com/steinbergmedia/vst3_pluginterfaces)는 `deps/vst3sdk/pluginterfaces`에 둡니다.

프로젝트 파일은 기본적으로 저장소 안의 `deps/` 디렉터리에서 의존성을 찾습니다.

- `deps/fftw`
- `deps/libsndfile`
- `deps/muparserx`
- `deps/tclap`
- `deps/highway`
- `deps/vst3sdk`

같은 기본값을 다음 환경 변수로 덮어쓸 수 있습니다.

- `FFTW_INCLUDE`, `FFTW_LIB`
- `LIBSNDFILE_INCLUDE`, `LIBSNDFILE_LIB`
- `MUPARSERX_INCLUDE`, `MUPARSERX_LIB`
- `TCLAP_ROOT`
- `HIGHWAY_INCLUDE`
- `VST3_SDK`

SIMD 변형 집합은 `.github/simd-variants.psd1` 한 곳에 정의합니다. 이 매니페스트가 CI 매트릭스, 태그와 SHA-256으로 고정한 의존성 다운로드, 설치 파일 채널 이름, 릴리스 노트를 모두 결정합니다. CI는 현재 다음 변형을 빌드합니다.

- `windows-x64-sse2`
- `windows-x64-avx`
- `windows-x64-avx2`
- `windows-x64-avx512`
- `windows-x64-avx10_1`
- `windows-arm64`

PR은 기본 변형인 `avx2`만 빌드합니다. `main` push는 자동 버전 bump로 새 버전이 나올 때만 여섯 개를 모두 빌드하며, 릴리스를 만들 수 없는 push(docs, CI, 리팩토링만 있는 변경)는 빌드 매트릭스를 건너뜁니다. 수동 `workflow_dispatch` 실행은 항상 여섯 개를 모두 빌드합니다. SIMD 매트릭스, 의존성 산출물 이름, 설치 파일 이름, 테스트 정책은 [docs/SimdBuildMatrix.md](docs/SimdBuildMatrix.md)에서 관리합니다.

Qt 도구는 CI에서도, 문서화된 로컬 설정에서도 qmake로 빌드합니다. Visual Studio 솔루션 전체를 빌드하려면 Qt VS Tools/QtMsBuild도 제대로 설정되어 있어야 합니다.

## 테스트

`Tests/`에는 프로젝트 여섯 개가 있습니다. `EditorLogicTests`와 `HybridConvTests`(단위 테스트), `EngineOrchestrationTests`(엔진 라우팅과 설정 교체 동작), `AudioRegressionTests`(엔진 출력을 커밋된 참조 데이터와 비교하며, CI에서는 SIMD 변형별로도 실행), `TestVst2Plugin`/`TestVst3Plugin`(VST2·VST3 호스트를 런타임에 시험하기 위한 자체 빌드 플러그인)입니다. 변형별 테스트 정책은 [docs/SimdBuildMatrix.md](docs/SimdBuildMatrix.md)에 함께 있습니다.
