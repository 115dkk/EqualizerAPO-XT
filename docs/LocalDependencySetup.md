# Local Dependency Setup

이 문서는 Windows 로컬 빌드에 필요한 의존성을 어떻게 준비하는지 정리합니다. 빌드 명령은 `CLAUDE.md`의 '빌드 명령' 절에 있습니다.

## 준비

저장소 루트에서 `setup-build.ps1`을 실행하십시오. CI의 모든 job이 쓰는 `.github/scripts/Initialize-Workspace.ps1`을 부르므로 로컬 체크아웃도 CI와 같은 순서로 준비됩니다.

```powershell
.\setup-build.ps1
```

스크립트가 하는 일은 다음과 같습니다.

- FFTW, muparserx, libsndfile, velopack_libc의 릴리스 자산과 Steinberg ASIO SDK를 받아 `.github/simd-variants.psd1`의 SHA-256과 대조한 뒤 `deps/` 아래에 풉니다. 이미 받은 압축 파일은 `deps/_downloads`에서 다시 씁니다.
- TCLAP, VST3 pluginterfaces, Highway를 매니페스트의 태그로 복제하고, 그 태그가 고정된 커밋을 가리키는지 확인합니다.
- Qt(매니페스트의 `Shared.QtVersion`)를 Qt 온라인 저장소에서 받아 `Qt/<버전>/msvc2022_64`에 설치하고, 그 qmake가 실행되며 같은 버전을 보고하는지 확인합니다.
- 의존성 경로(`FFTW_INCLUDE`, `FFTW_LIB`, `LIBSNDFILE_INCLUDE`, `LIBSNDFILE_LIB`, `MUPARSERX_INCLUDE`, `MUPARSERX_LIB`, `TCLAP_ROOT`, `VST3_SDK`, `HIGHWAY_INCLUDE`, `ASIO_SDK`, `VELOPACK_INCLUDE`, `VELOPACK_LIB`)와 `QT_ROOT`를 현재 PowerShell 세션에 내보내고, Qt의 `bin` 폴더를 `PATH` 앞에 넣습니다.

기본 변형(avx2)이 아닌 변형을 준비하려면 `-SimdVariant`를 주십시오. sse2와 avx는 FFTW와 libsndfile을 vcpkg로 빌드하므로 시간이 더 걸립니다. Qt가 이미 있으면 `-SkipQt`로 건너뛸 수 있습니다.

## 필요한 도구

- Visual Studio 2026 Build Tools(또는 2022). `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`와 ATL 구성 요소가 있어야 합니다. ATL이 없으면 `EqualizerAPO.dll`을 링크할 때 `atls.lib`를 찾지 못합니다.
- Python 3. `7z`가 `PATH`에 없는 PC에서는 Qt 아카이브를 `py7zr`로 풉니다.
- Git. 소스 의존성을 복제할 때 씁니다.

## 설치 위치

준비한 것은 모두 저장소 아래에 있고 커밋하지 않습니다.

- `deps/fftw`, `deps/libsndfile`, `deps/muparserx`, `deps/velopack_libc`
- `deps/tclap`, `deps/vst3sdk`, `deps/highway`, `deps/asiosdk`
- `deps/_downloads`
- `Qt/`

`.vcxproj`(`Directory.Build.props`)와 `.pro` 파일의 기본 경로는 이 배치를 가리킵니다. 다른 곳에 둔 의존성을 쓰려면 위 환경 변수를 직접 설정하십시오.

## 주의할 점

Qt 앱(Editor, DeviceSelector)은 `.vcxproj`가 없고 qmake로만 빌드합니다. `EqualizerAPO.sln`에는 MSBuild 프로젝트만 있습니다. 두 `.pro` 파일은 x64 빌드에서 `EAPO_SIMD_FLAGS`(또는 SSE2 기준선의 `EAPO_SIMD_BASELINE=1`)가 없으면 qmake 단계에서 실패하며, `.github/scripts/Build-QtApps.ps1`이 변형에 맞는 값을 매니페스트에서 읽어 넘깁니다.

받은 의존성은 Release 구성용입니다. 엔진을 쓰는 프로젝트의 Debug 구성은 이 라이브러리로 링크되지 않습니다(`EngineConsumer.props`에 적힌 결정입니다).
