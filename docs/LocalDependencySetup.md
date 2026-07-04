# Local Dependency Setup

이 문서는 Windows 로컬 빌드에 필요한 의존성 배치를 정리합니다.

## 결론

GitHub Actions artifact를 직접 받는 방법은 CI 설정과 방향이 맞습니다. 다만 2026-05-22 기준 TheFireKahuna 쪽 최신 성공 빌드 artifact는 모두 만료되어 바로 내려받을 수 없습니다.

지금은 GitHub Release 자산을 받는 방법이 더 안정적입니다. `tclap`은 artifact가 아니라 저장소를 체크아웃합니다.

## 설치 위치

로컬 설치물은 모두 저장소 아래에 두고 커밋하지 않습니다.

- `deps/fftw`
- `deps/libsndfile`
- `deps/muparserx`
- `deps/tclap`
- `Qt/`
- `tools/`

`.vcxproj`와 `Editor.pro`의 기본 경로는 위 `deps/` 배치를 보도록 맞췄습니다. 필요하면 기존처럼 환경 변수로 덮어쓸 수 있습니다.

주요 변수는 다음과 같습니다.

- `FFTW_INCLUDE`, `FFTW_LIB`
- `LIBSNDFILE_INCLUDE`, `LIBSNDFILE_LIB`
- `MUPARSERX_INCLUDE`, `MUPARSERX_LIB`
- `TCLAP_ROOT`

## 설치한 항목

- AOCL-FFTW 5.1: `fftw-windows-release-x64-avx2.zip`
- muparserx 4.0.13: `muparserx-msvc-release-x64-avx2.zip`
- libsndfile 1.2.2: `libsndfile-x64-avx2.zip`
- TCLAP 1.2.5: `115dkk/tclap` tag `1.2.5`
- Qt 6.10.1 `win64_msvc2022_64`: `qtbase`, `qttools`, `qtsvg`, `qttranslations`
- 7-Zip `7zr.exe`: Qt 7z 패키지 압축 해제용
- Visual Studio Build Tools 2026 ATL: `Microsoft.VisualStudio.Component.VC.14.51.ATL`

## 빌드 방법

비-Qt 프로젝트는 MSBuild로 빌드합니다.

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' Common.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' EqualizerAPO\EqualizerAPO.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' Benchmark\Benchmark.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' VoicemeeterClient\VoicemeeterClient.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2
```

Qt 프로젝트는 CI와 같은 방식으로 qmake/nmake를 씁니다. 먼저 VS 개발자 환경과 Qt 경로를 잡아야 합니다.

```cmd
set "PATH=%CD%\Qt\bin;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

각 프로젝트는 별도 빌드 폴더에서 `lrelease`, `qmake`, `nmake` 순서로 빌드합니다.

```cmd
mkdir build-Editor-x64
cd build-Editor-x64
..\Qt\bin\lrelease.exe ..\Editor\Editor.pro
..\Qt\bin\qmake.exe ..\Editor\Editor.pro -r "CONFIG+=release" "EAPO_UPDATE_CHANNEL=x64-avx2" "EAPO_SIMD_FLAGS=/arch:AVX2"
nmake /NOLOGO
```

`DeviceSelector`와 `UpdateChecker`도 같은 방식으로 빌드합니다. 세 `.pro` 파일 모두 x64 빌드에서 `EAPO_SIMD_FLAGS`(또는 SSE2 기준선의 `EAPO_SIMD_BASELINE=1`)가 없으면 qmake가 `error()`로 실패합니다. 변형별 플래그와 채널 값은 `.github/simd-variants.psd1`을 기준으로 합니다. 빌드 뒤에는 다음처럼 Qt 런타임을 배치합니다.

```powershell
& .\Qt\bin\windeployqt.exe .\build-Editor-x64\release\Editor.exe --release --no-opengl-sw
& .\Qt\bin\windeployqt.exe .\build-DeviceSelector-x64\release\DeviceSelector.exe --release --no-opengl-sw
& .\Qt\bin\windeployqt.exe .\build-UpdateChecker-x64\release\UpdateChecker.exe --release --no-opengl-sw
```

## 주의할 점

`EqualizerAPO.sln` 전체 MSBuild는 Qt VS Tools의 `QtMsBuild` 파일이 없으면 Qt `.vcxproj`에서 실패합니다. CI도 이 문제를 피하려고 Qt 앱은 qmake로 따로 빌드합니다.

전체 솔루션을 Visual Studio에서 바로 빌드하려면 Qt VS Tools 또는 호환되는 `QtMsBuild` 배치가 추가로 필요합니다.
