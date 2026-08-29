# 개발자 문서
**EqualizerAPO-XT**의 개발자 문서입니다. 소스를 살펴보거나 빌드하거나 시험하려는 사람, 또는 직접 APO를 만들려는 사람을 위한 내용입니다. 소프트웨어를 쓰기만 할 거라면 [사용자 문서](Korean-Documentation)를 보세요.

*면책: 이 문서는 아는 한 정확하게 작성했습니다. 완전하지도, 오류가 없다고 보장하지도 못합니다.*
## EqualizerAPO-XT 빌드
XT는 Windows 프로젝트입니다. C++ 프로젝트는 Visual Studio 2022 / 2026 도구와 Windows 10 SDK를 기준으로 하고, GUI 도구는 Qt로 빌드합니다. 원본 Equalizer APO 빌드는 라이브러리를 `C:\Program Files` 아래에 설치해야 했지만, XT는 모든 의존성을 로컬 `deps/` 폴더에 두고 이를 받아오는 설정 스크립트를 제공합니다.

### 사전 준비물
* **Visual Studio 2022 빌드 도구**(또는 2026). `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`와 `Microsoft.VisualStudio.Component.VC.ATL`을 반드시 포함합니다. ATL이 필요합니다. 없으면 `EqualizerAPO.dll` 링크 단계에서 `atls.lib`를 찾지 못합니다. XT 프로젝트는 VS 2026 도구 집합 `v145`로 빌드하며, VS 2022 환경에서는 `/p:PlatformToolset=v143`을 넘깁니다.
* **Python 3** — `aqtinstall`로 Qt를 설치할 때 씁니다.
* **Qt 6.10.1 (MSVC 2022 x64)** — `Qt/6.10.1/msvc2022_64/`에 설치합니다. 설정 편집기, 장치 선택기, 업데이트 확인기에 필요합니다.
* **외부 라이브러리** — 저장소에 **커밋하지 않습니다**: [FFTW](https://www.fftw.org/), [libsndfile](https://libsndfile.github.io/libsndfile/), [muParserX](https://github.com/beltoforion/muparserx), [TCLAP](http://tclap.sourceforge.net/), [Google Highway](https://github.com/google/highway), MIT 라이선스인 Steinberg VST3 *pluginterfaces*, 그리고 ASIO 래퍼용 Steinberg ASIO SDK 2.3.4(SHA-256으로 고정해 받고, SDK의 GPL 버전 3 선택지로 씀)입니다. 빌드 시점에 `deps/` 아래에 배치합니다.
* **Velopack** (`vpk` CLI, `dotnet tool install -g vpk`) — CI가 바이너리를 `Setup.exe` / `.nupkg` 산출물로 패키징할 때만 씁니다. 로컬 개발에는 필요 없습니다.

**`setup-build.ps1`** 스크립트가 (SIMD 변형별) 바이너리 의존성을 받고, 헤더 전용 라이브러리(TCLAP, Highway, VST3 pluginterfaces)를 클론하며, Qt를 설치합니다. 정확한 배치와 MSBuild / qmake 명령은 [setup-build.ps1](https://github.com/115dkk/EqualizerAPO-XT/blob/main/setup-build.ps1)과 [로컬 의존성 설정](https://github.com/115dkk/EqualizerAPO-XT/blob/main/docs/LocalDependencySetup.md)을 보세요.

### 소스 코드 구성
저장소는 다음 구성 요소로 나뉩니다. `EqualizerAPO.sln`에는 네이티브 MSBuild 프로젝트가 들어 있고, 일반 CI 경로에서 Editor는 `Editor/Editor.pro`로 빌드합니다.

* **Common** — 필터 엔진(`FilterEngine`, `FilterConfiguration`, `IFilter`), `filters/`의 필터 구현, `parser/`의 muParserX 확장, `audio/`, `dsp/`, `platform/`, `runtime/`, `services/`, `text/`, `vst/`의 책임별 공용 모듈이 들어 있는 정적 라이브러리입니다. 컨볼루션 코드는 `libHybridConv-0.1.1/`에 있습니다. 다른 프로젝트가 여기에 링크합니다.
* **EqualizerAPO** — Audio Processing Object DLL(`EqualizerAPO.dll`)입니다. COM 보일러플레이트를 담고 APO 인터페이스를 구현하며 Common 필터 엔진을 호출합니다. ATL 기반이라 `atls.lib`가 필요합니다.
* **Editor** — Qt 기반 설정 편집기입니다. `Editor.exe`가 Velopack 패키지의 메인 실행 파일이며, `services/install/ApoRegistration`과 `services/update/VelopackBootstrap`을 통해 모든 Velopack 설치/업데이트/제거 훅을 처리합니다.
* **DeviceSelector** — 처음 설치한 뒤 사용자가 APO를 등록할 오디오 장치를 고르도록 보여 주는 Qt 도구입니다. 원본의 Configurator를 대체합니다.
* **UpdateChecker** — 로그온할 때 실행되어 빌드 채널에 새 릴리스가 있으면 알려 주는 Qt 도구입니다.
* **Benchmark** — 장치에 설치하지 않고 오디오 처리를 시험하는 콘솔 프로그램입니다. 필터 종류를 실험하거나 성능을 잴 때 편합니다.
* **VoicemeeterClient** — Voicemeeter 연동용 보조 프로그램입니다.
* **SubwooferRoutingCore** — APO 명령과 독립 플러그인이 함께 쓰는 MIT 라이선스 서브우퍼 라우팅 DSP 및 상태 컴파일러입니다.
* **SubwooferRoutingVst3** — 공용 코어에 링크하는 독립 `EAPO XT Subwoofer Routing` VST3 플러그인입니다.
* **Installer** — 아키텍처와 SIMD 지원을 감지하고 선택한 릴리스 파일을 검증한 뒤 Velopack 설치 프로그램을 실행하는 작은 Win32 `EqualizerAPO-XT-Setup.exe` 진입 파일입니다.
* **EqualizerAPOAsio** — DAW가 불러오는 ASIO 래퍼 DLL(`EqualizerAPOAsio.dll`)입니다. 응용 프로그램 쪽으로는 ASIO 드라이버이고, 뒤에는 하드웨어 ASIO 드라이버 또는 WASAPI 독점 모드로 연 Windows 엔드포인트(`asio/WasapiExclusiveTarget`)가 있으며, 버퍼마다 처리기 시임을 거쳐 엔진 호스트로 갑니다. x64 빌드는 `x86\` 아래 32비트 래퍼도 만들고, ARM64는 만들지 않습니다.
* **EqualizerAPOHost** — `EqualizerAPOHost.exe`, 래퍼가 공유 메모리 링(`runtime/ipc/StreamRing`)으로 대화하는 별도 프로세스의 엔진 호스트입니다. 세션당 하나이고, 필요할 때 뜨며, 마지막 스트림 뒤 1분이면 사라집니다.
* **Tests** — 테스트 모음 `HybridConvTests`, `EditorLogicTests`, `EngineOrchestrationTests`, `AudioRegressionTests`, `AsioTests`, 호스트 시험용 `TestVst2Plugin`과 `TestVst3Plugin`, 픽스처 `FakeAsioDriver`와 `AsioSupport`, 프로브 네 개입니다. `ApoHostProbe`(오디오 엔진 없이 엔드포인트 하나에 대해 `EqualizerAPO.dll`을 띄움), `CaptureProbe`(한 엔드포인트에 톤을 재생하고 다른 엔드포인트에서 WASAPI로 잼. 작은 엔진 주기 포함), `AsioProbe`(DAW가 여는 방식으로 래퍼를 통해 대상을 엶), `VstPreviewProbe`입니다.
* **Setup** — CI 패키징 단계가 Velopack 출력에 넣는 예제 설정 파일(`config/`)과 문서 `.url` 바로 가기입니다.

CI는 `.github/workflows/build.yml`에 있습니다. x64 `sse2`, `avx`, `avx2`, `avx512`, `avx10_1` 변형과 ARM64 `neon`을 빌드한 뒤 `vpk pack`으로 채널별 설치 파일을 만듭니다([설치](Korean-Documentation#설치) 절과 [SIMD 빌드 행렬](https://github.com/115dkk/EqualizerAPO-XT/blob/main/docs/SimdBuildMatrix.md) 참고). 풀 리퀘스트는 AVX2만 빌드합니다. `main` push는 자동 버전 단계가 새 버전을 만들 때만 여섯 변형을 모두 빌드해 릴리스를 만들며, 문서·CI·리팩터링만 바뀐 push는 빌드 행렬을 건너뜁니다. 수동 워크플로 실행은 항상 여섯 변형을 모두 빌드합니다. 빌드 옆에서 `cppcheck`(기준선 0의 차단 게이트), `memcheck`(런타임 테스트를 AddressSanitizer로 다시 빌드), `pester`(빌드 스크립트 자체 테스트), `cross-variant-compare`, `capture-gate`가 돕니다. 캡처 게이트는 호스팅 러너에 VB-CABLE 드라이버(SHA-256 고정)를 설치하고, 빌드한 제품을 자체 설치 훅으로 스테이징한 뒤, 장치 선택기로 케이블에 APO를 등록해 녹음 앱이 기본 모드와 communications 모드에서 설정한 프리앰프를 듣는지 재고, 컨볼루션을 넣은 설정으로 재생 쪽을 엔진의 최소 주기에서 재며, 엔드포인트의 ASIO 항목을 DAW처럼 COM으로 엽니다. 패키징 단계는 미리 컴파일된 헤더나 생성 소스가 설치 파일에 들어가면 실패하고(v2.38.0부터 v2.48.0까지의 설치 파일이 그 때문에 약 250 MB였습니다), windeployqt가 목록에 없는 플러그인 폴더를 내놓아도 실패합니다. `publish-wiki.yml`은 `Wiki/github-wiki/` 변경을 이 위키에 별도로 게시합니다. 원본의 NSIS 설치는 제거되었습니다.

## APO 개발
APO(Audio Processing Object)는 샘플 데이터가 장치 드라이버에 가기 전에 처리하도록 Windows 오디오 서비스가 불러오는 사용자 공간 모듈입니다. APO는 보통 오디오 드라이버와 함께 배포되며, 오디오 DRM을 우회하지 못하도록 서명되어 있습니다. 두 종류가 있습니다. **GFX**(전역 효과, 스트림을 믹싱한 뒤 적용)와 **LFX**(로컬 효과, 믹싱 전에 적용)입니다. 출력 장치에는 GFX 하나와 LFX 하나를, 입력 장치에는 LFX 하나를 등록할 수 있습니다. APO는 GUID로 식별되는 COM 객체로 등록됩니다. Microsoft의 [Custom Audio Effects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-vista-custom-audio-effects)와 [Reusing System Effects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/reusing-windows-vista-system-effects) 문서가 이 모델과 예제를 설명합니다.

사용자 APO를 추가하려면 두 가지를 넘어야 합니다. 오디오 엔진이 서명되지 않은 APO를 불러오도록 허용해야 하고, 장치의 기존 APO를 사용자 APO에 연결해 계속 동작하게 해야 합니다. Windows 오디오 서비스가 DLL을 불러오게 만드는 레지스트리 변경은 이렇습니다.

1. `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio`에서 DWORD **`DisableProtectedAudioDG`**를 `1`로 설정합니다. APO 서명 검사를 꺼서 서명되지 않은 APO가 로드되게 합니다. 보호된 오디오 경로가 필요한 애플리케이션은 동작이 바뀌거나 출력을 거부할 수 있습니다.
1. APO COM 클래스를 `HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\<GUID>`에 등록합니다. 키 기본값으로 클래스 이름을 두고, `InprocServer32` 하위 키를 만들어 기본값을 DLL 경로로 설정하며, `ThreadingModel`을 적절히 설정합니다.
1. `HKEY_LOCAL_MACHINE\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\<GUID>`를 만듭니다. 보통 DDK 함수 `RegisterAPO`(`audioenginebaseapo.h`에 선언)가 처리하며, `UnregisterAPO`가 제거합니다.
1. `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\<엔드포인트 GUID>\FxProperties`에서 장치별로 APO를 등록합니다(`Render` 경로는 출력 장치, `Capture`는 입력 장치). `FxProperties`에서 `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1` 값은 LFX APO의 GUID를, `...,2`는 GFX APO의 GUID를 담습니다. 보통 이미 드라이버 APO를 가리키므로, 사용자 APO를 등록하려면 값 하나를 교체하고 원래 값을 다른 곳에 저장합니다(Equalizer APO는 `HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO\Child APOs`에 저장). 제거할 때 원래 값을 복원하기 위해서이기도 하고, 사용자 APO가 기존 APO를 불러 호출해 그 기능을 계속 수행하게 하기 위해서이기도 합니다. Windows 8.1부터는 `,5`(LFX)와 `,6`(GFX)으로 끝나는 값도 쓰이며, 이 값으로 등록하면 옛 `,1`/`,2` 값으로 등록한 APO는 무시됩니다. 또 8.1부터 `{d3993a3f-99c2-4402-b5ec-a92a0367664b},5`와 `,6` 값이 처리 모드를 지정하며(MULTI_SZ 형식), 목록에 없는 모드에서는 엔진이 APO를 만들지 않습니다. XT는 자기가 채우는 슬롯을 그 방향의 모든 모드에 올리고(녹음은 Default `{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}`, Communications `{98951333-B9CD-48B1-A0A3-FF40682D73F7}`, Speech `{FC1CFC9B-B9D6-4CFA-B5E0-4BB2166878B2}`, 재생은 Default·Media·Movie·Communications·Notification), 드라이버가 써 둔 목록은 그대로 둡니다. Raw는 올리지 않습니다. raw 스트림은 설계상 스트림 효과를 건너뜁니다. 녹음 엔드포인트에는 pre-mix APO 하나만 들어가며, `FxProperties`를 아예 발행하지 않는 드라이버는 레거시 `,1`/`,2` 값으로 등록합니다. 엔진이 그런 드라이버를 레거시 슬롯으로만 처리하기 때문입니다.

XT에서는 이 등록과 정리를 `services/install/ApoRegistration`이 수행하며, Editor가 처리하는 Velopack 훅에서 구동됩니다. APO는 장치 테스트 파이프 이름으로 글자, 숫자, `_`, `-`만(128자까지) 받아들이고 그 밖에는 테스트가 없는 것처럼 동작하며, 레지스트리 포트의 읽기 메서드는 저장소 안의 CodeQL 모델 팩에 소스로 선언되어 주간 스캔이 레지스트리에서 온 경로를 계속 봅니다.

ASIO 항목의 등록은 다릅니다. `HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\<항목 이름>`이 래퍼의 CLSID를 가리키고, `SOFTWARE\Classes\CLSID` 아래 클래스 트리가 `EqualizerAPOAsio.dll`을 가리키며, `SOFTWARE\EqualizerAPO\ASIO\<래퍼 CLSID>`의 기록이 DLL에 무엇을 불러올지 알려 줍니다. 하드웨어 드라이버의 CLSID이거나, (`TargetKind` 1) WASAPI 독점 모드로 열 재생·녹음 엔드포인트 GUID입니다. 래퍼 CLSID는 대상의 CLSID나 엔드포인트 GUID에서 파생되므로 같은 대상은 늘 같은 항목이 됩니다.

## 함께 보기
* [사용자 문서](Korean-Documentation) — EqualizerAPO-XT 사용하기.
* [설정 레퍼런스](Korean-Configuration-reference) — 설정 명령 집합.
* [English version of this page](Developer-documentation)
