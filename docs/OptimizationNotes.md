# Optimization Notes

이 문서는 `codex/optimization-pass` 작업에서 확인한 최적화 후보와 처리 결과를 정리합니다.

## 확인 범위

- C/C++ 소스와 헤더 285개, 약 3만 줄을 확인했습니다.
- 주요 대상은 오디오 처리 경로, 필터 구현, VST 처리, 설정 파싱, Qt 도구, 빌드 파일입니다.
- 빌드 파일은 `EqualizerAPO.sln`, 각 `.vcxproj`, Qt `.pro`, NSIS `.nsi`, GitHub Actions 파일을 기준으로 확인했습니다.

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
- 대상 파일은 `AnalysisThread`, `ConvolutionFilter`, `GraphicEQFilter`, `RegistryHelper`, `StringHelper`, `VoicemeeterAPOInfo`입니다.

### 설정과 보조 코드

- `ChannelHelper::getChannelNames`에서 결과 벡터 크기를 미리 예약하고, 맵 조회를 한 번만 하도록 바꿨습니다.
- `StringHelper::join`은 `wstringstream` 대신 크기를 미리 예약한 `std::wstring`에 붙이도록 바꿨습니다.
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
- Qt 도구는 가능한 경우 `Editor`, `DeviceSelector`, `UpdateChecker`의 qmake 빌드를 확인합니다.
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
