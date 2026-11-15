# EqualizerAPO-XT 기술 부채 감사

| 항목 | 값 |
|---|---|
| 저장소 | EqualizerAPO-XT |
| 날짜 | 2026-11-15 |
| 커밋 | `bf1a77b3` (main, `version.h` 2.63.2) |
| 모드 | CI. 질문은 전부 맨 아래 '메인테이너에게 묻는 것'에 모았고 아무것도 커밋하지 않았습니다 |
| 범위 | 저장소 전체. `docs/`, `Wiki/`, 번역, 이미지, 글꼴, 기준 오디오는 코드나 빌드 문제를 드러낼 때만 다뤘습니다 |
| 기준(baseline) | 없음. `TECH_DEBT_AUDIT.md`는 이 체크아웃에 없었고 git 이력에도 없습니다. 이 파일이 첫 기준입니다. 직전 감사(태그 `audit/20261015-201014`)의 결과는 PR #423에서 처리됐습니다 |
| 진행 방식 | 모듈별 병렬 감사(하위 감사 8건) 뒤 주 감사자가 발견을 코드와 실행으로 다시 확인하고 병합했습니다 |
| 아키텍처 검토 | 같은 실행의 `ARCHITECTURE_AUDIT.md`를 읽었고, 그 'Fine as is' 항목은 나누거나 합치자고 하지 않았습니다 |

**실행한 것.** avx2 구성으로 MSBuild 프로젝트 20개를 빌드했고 경고는 0건이었습니다. EditorLogicTests(4150 checks), HybridConvTests(1647과 하위 스위트), EngineOrchestrationTests(11399), AsioTests(631과 하위 스위트 넷)가 통과했습니다. 그 뒤 `Benchmark.exe -v`에 시험용 설정 여덟 개를 넣어 돌렸고, EditorLogicTests와 EngineOrchestrationTests에 확인용 테스트 세 건을 잠깐 넣어 실행한 다음 되돌렸습니다. Windows 엔드포인트 열거기에는 PowerShell로 직접 물었습니다.

**쓰지 못한 도구.** `lizard`는 설치가 거부돼 함수 길이를 자체 스크립트로 쟀습니다(복잡도 수치는 없습니다). `cppcheck`는 CI가 2.21.0으로 0건 게이트를 돌리는데 이 러너에는 2.14.0뿐이라 결과가 비교되지 않아 돌리지 않았습니다. `clang-tidy`는 `compile_commands.json`이 없어 돌리지 않았습니다. `gh`가 로그인돼 있지 않아 CodeQL 알림과 GitHub의 태그, 릴리스는 보지 못했습니다. Editor와 DeviceSelector(qmake)는 빌드하지 않았습니다.

**검증 표시.** 표의 설명 끝에 붙은 `[실행]`은 이번 감사에서 실행으로 재현한 것, `[코드]`는 주 감사자가 해당 줄을 읽어 확인한 것, `[보고]`는 하위 감사가 줄 번호와 함께 보고했고 주 감사자가 일부만 대조한 것입니다.

**ID.** 기준이 없으므로 `TD-01`부터 새로 매겼고 모두 `NEW`입니다. 코드 주석에 남은 `audit #348 TD-NN` 같은 번호는 이슈별 번호라 이 파일의 번호와 다릅니다. 이 파일의 번호를 인용할 때는 `TECH_DEBT_AUDIT TD-NN`으로 적으면 구분됩니다.

---

## 요약

발견은 80건입니다(Critical 1, High 3, Medium 33, Low 43). 영향이 큰 순서로 적습니다.

- **설치 훅의 설정 마이그레이션이 사용자 파일을 지웁니다(TD-01).** 예전 설정이 다른 드라이브나 공유 폴더의 파일을 절대 경로로 가리키면, 복사 대상 경로가 원본과 같아져 원본을 먼저 지우고 복사에 실패합니다. 실행으로 재현했습니다.
- **카드의 '가져오기'도 같은 방식으로 원본을 지웁니다(TD-02).** 설정 폴더의 바로 아래 폴더에 있는 파일을 가져오면 대상이 원본 자신입니다. 실행으로 재현했습니다.
- **`Copy:`가 없는 채널 이름을 상수로 바꿉니다(TD-03).** 스테레오 장치에서 `Copy: L=C`는 L 채널에 풀스케일 DC를 내보냅니다. 실행으로 재현했습니다.
- **릴리스 파이프라인이 배포할 설치 파일과 업데이트·제거 훅을 한 번도 실행하지 않고 게시합니다(TD-04).** 자동 업데이트로 나가는 제품인데 첫 실행이 사용자 기기입니다.
- **엔진의 두 가지 메모리 안전 계약이 호출자에게 맡겨져 있습니다(TD-09, TD-10).** 재초기화 때 게시 권한이 두 번 나가는 것을 실행으로 확인했고, `frameCount`가 `maxFrameCount`를 넘지 않는다는 보장은 엔진 입구에 없습니다.
- **설정 줄의 실패가 조용합니다(TD-06, TD-07, TD-08, TD-11).** 없는 IR 파일은 아무 로그도 남기지 않고, 정규식이 잘못된 `If:`는 본문을 실행하며, 불안정한 IIR과 과도한 Preamp는 `inf`를 내보냅니다. 모두 실행으로 재현했습니다.
- **Editor의 장치 형식 확인이 한 번도 성공하지 못합니다(TD-05).** 엔드포인트 ID 대신 GUID만 넘기기 때문이고, 열거기는 그것을 형식 오류로 거절합니다. 'EQ가 적용되지 않는 장치' 경고와 도구 모음의 형식 배지가 여기에 달려 있습니다.
- **로그에서 한글이 `?`로 바뀝니다(TD-12).** 사용자 이름이 한글인 프로필 경로와 한국어 오류 문구가 로그에서 읽을 수 없게 됩니다.
- **권한이 높은 실행 파일이 가장 덜 보호돼 있습니다(TD-15, TD-16, TD-17, TD-24).** 승격된 채 남는 보조 프로세스가 사용자가 고칠 수 있는 설정의 플러그인을 불러오고, Qt 앱 둘은 `/guard:cf`, `/sdl`, `/WX` 없이 빌드됩니다.
- **격주 감사 자신도 깨져 있습니다(TD-23).** Windows 러너에서 pathspec 파일이 CRLF로 풀려 변경 요약이 항상 빕니다. 이번 실행의 `change-summary.txt`도 머리글 두 줄뿐이었습니다.

---

## 아키텍처 모델

제품은 Windows 오디오 서비스(audiodg, LOCAL SERVICE) 안에 올라가는 APO DLL 하나와 그 주변 도구들입니다. APO는 `Common.lib`의 `FilterEngine`을 품고, 엔진은 `config.txt`를 줄 단위로 읽어 21개 필터 팩토리에 차례로 제시해 필터 사슬을 만든 뒤 `ConfigSwapChannel`을 거쳐 오디오 스레드에 넘깁니다. 같은 엔진을 Benchmark, VoicemeeterClient, ASIO 래퍼 DLL이 띄우는 `EqualizerAPOHost.exe`, Editor의 분석 스레드가 각자 띄웁니다. 플러그인 호스트(`vst/`)와 convolution(`libHybridConv`, FFTW)은 엔진 안에서 제3자 코드와 파일을 다루는 자리입니다. 장치에 APO를 붙이고 떼는 일은 `devices/`가 레지스트리 포트(`IRegistry`)와 트랜잭션 위에서 하고, 승격이 필요한 DeviceSelector가 그것을 씁니다. 설치와 업데이트는 Velopack이 맡으며 Editor가 `--veloapp-*` 훅으로 COM 등록, ACL 부여, 설정 마이그레이션을 합니다.

Editor는 Qt 앱으로, 엔진 소스를 직접 컴파일하고(`Editor.pro`), 문서 모델(`FilterListModel`) 위에 카드 편집기와 다섯 스킨을 올립니다. 코드의 절반가량이 Editor이고 그중 7,500줄쯤은 CI 게이트가 배포용 실행 파일을 직접 몰기 위한 갤러리·프로브 코드입니다. 빌드는 MSBuild(엔진, DLL, 테스트)와 qmake(Qt 앱 둘)로 나뉘고 CI는 SIMD variant 여섯 개를 `.github/simd-variants.psd1` 하나에서 펼칩니다. README의 설명과 어긋나는 구조는 찾지 못했습니다. 다만 빌드 절차를 적은 문서 여럿이 지금의 스크립트와 다릅니다(TD-31, TD-76).

신뢰 경계는 넷입니다. 사용자가 손으로 고치는 설정 글과 그것이 가리키는 파일, 제3자 플러그인, 프로세스 사이의 공유 메모리와 파이프, 그리고 승격된 프로세스가 읽는 사용자 소유의 경로와 레지스트리입니다. 이번 감사에서 무게가 실린 발견은 대부분 이 넷 가운데 하나에서 값이 검증 없이 넘어가는 자리였습니다.

---

## 발견

분류 번호는 감사 차원입니다(1 구조 부식, 2 일관성, 3 타입·계약, 4 테스트, 5 의존성·설정, 6 성능·자원, 7 오류 처리·관측, 8 보안, 9 문서).

| ID | 상태 | 분류 | 파일:줄 | 심각도 | 공수 | 설명 | 권고 |
|---|---|---|---|---|---|---|---|
| TD-01 | NEW | 3 | `Editor/import/ConfigDependencyScanner.cpp:84-91`, `Editor/import/ImportExecutor.cpp:44-64`, `Editor/import/LegacyMigration.cpp:143-157` | Critical | S | 설치·업데이트 훅의 마이그레이션이 예전 설정이 가리키는 파일을 지웁니다. `QDir::relativeFilePath`는 드라이브가 다르면 절대 경로를 그대로 돌려주는데 `relativeToRoot`가 `..`만 거르므로 대상 경로가 원본과 같아지고, 실행기는 대상을 지운 뒤 복사합니다. `Convolution: D:\IR\room.wav`, `VSTPlugin: Library D:\VST\x.dll`, UNC `Include:`가 해당합니다. 경고도 남지 않습니다. `[실행]` | `relativeToRoot`가 절대 경로 결과를 거절하게 하고, 실행기는 대상이 설정 폴더 밖이거나 원본과 같은 파일이면 건너뜁니다. 임시 이름으로 복사한 뒤 바꿔치기합니다. `ConfigImportTests`에 다른 드라이브 사례를 넣습니다 |
| TD-02 | NEW | 3 | `Editor/import/ImportExecutor.cpp:54-64`, `Editor/import/ConfigDependencyScanner.cpp:94-100`, `Editor/widgets/cards/FileReferenceController.Dialogs.cpp:82-114`, `Editor/widgets/cards/ConvolutionCardEditor.cpp:182-190` | High | S | 카드의 '가져오기'가 설정 폴더 바로 아래 폴더에 있는 파일(`config\IR\room.wav`)을 지웁니다. 대상이 `<폴더 이름>/<파일>`로 계산돼 원본 자신이 되기 때문입니다. 이 버튼은 오디오 서비스가 읽지 못하는 파일에 뜨는데, 다른 곳에서 옮겨 와 ACL이 그대로인 파일이 바로 그 경우입니다. `[실행]` | TD-01과 같은 가드로 막고, 이미 제자리에 있는 파일은 복사 대신 접근 권한만 고칩니다 |
| TD-03 | NEW | 3 | `filters/CopyFilter.cpp:61-78`, `:143-159`, `audio/ChannelLayout.cpp:127-168` | High | S | `Copy:`의 원본 채널 이름이 장치에 없으면 채널 번호 -1이 되고, -1은 '상수 항'과 같은 값이라 계수가 DC로 더해집니다. 스테레오에서 `Copy: L=C`는 L에 +1.0, `Copy: L=L+0.5*C`는 최고 1.5(클립 43,725샘플)를 냅니다. 로그는 `Invalid channel position C`뿐이고 Copy 줄을 가리키지 않습니다. `[실행]` | 상수와 '풀리지 않은 이름'을 다른 값으로 나누고, 풀리지 않은 항은 버리면서 줄 번호와 함께 파싱 오류로 보고합니다(`SendFilterFactory.cpp:173-190`이 같은 일을 이미 합니다) |
| TD-04 | NEW | 4 | `.github/scripts/New-VelopackRelease.ps1:131-209`, `.github/scripts/Invoke-CaptureGate.ps1:669`, `Editor/main.cpp:230-275` | High | M | 게시 전에 포장된 설치 파일을 실행하는 job이 없습니다. CI가 부르는 훅은 `--veloapp-install` 하나이고 `--veloapp-updated`, `--veloapp-obsolete`, `--veloapp-uninstall`과 승격 재실행은 어느 게이트에서도 돌지 않습니다. Qt 플러그인을 `qt\` 아래로 옮기는 배치도 포장 단계에서만 생깁니다. 실제 설치와 제거를 하는 워크플로 둘은 수동 실행이고 이미 게시된 릴리스를 씁니다. `[코드]` | `create-release`에서 포장과 게시를 나누고, 그 사이에 기본 채널의 Setup.exe를 조용히 설치해 `Editor.exe --selftest-vst`를 돌린 뒤 `Update.exe --uninstall` 하고 AudioSrv가 떠 있는지와 EQ CLSID가 남지 않았는지 확인합니다 |
| TD-05 | NEW | 3 | `services/audio/AudioFormatProbe.cpp:58`, `Editor/MainWindow.cpp:330-341`, `Editor/MainWindowParts/MainWindow.Device.cpp:218` | Medium | S | `AudioFormatProbe::probe`가 `getDeviceGuid()`의 GUID만으로 `IMMDeviceEnumerator::GetDevice`를 부릅니다. 이 러너에서 물어보니 GUID만 넘기면 `0x80070057`(형식 오류), `{0.0.0.00000000}.` 접두를 붙이면 `0x80070490`(없는 장치)입니다. 그래서 결과가 항상 `Unknown`이고, 'EQ inactive on some devices' 경고와 형식 배지가 동작하지 않습니다. 이 함수를 부르는 테스트는 없습니다. `[실행]` | 입력·출력에 맞는 접두를 붙입니다. 접두 규칙은 `asio/WasapiExclusiveTarget.cpp:220`, `devices/EndpointProbe.cpp:81`, `EqualizerAPOHost/Keepalive.cpp:87`에 이미 세 번 있으니 함수 하나로 모읍니다(TD-47) |
| TD-06 | NEW | 7 | `filters/ConvolutionFilterFactory.cpp:46-50`, `filters/MultiConvolutionFilterFactory.cpp:46-50`, `filters/IrCache.cpp:117-121`, `filters/ConvolutionFilter.cpp:117-121` | Medium | S | `Convolution:`과 `MultiConvolution:`이 없는 파일을 가리키면 로그도 파싱 오류도 없습니다. "was not found" 문구는 `path.empty()`일 때만 나가는데 없는 파일은 경로가 채워진 채 핸들만 비어 옵니다. `[실행]` | `ConfigFileReference`가 '열린 파일 또는 이유 한 문장'을 돌려주게 합니다(`ARCHITECTURE_AUDIT.md` 후보 2) |
| TD-07 | NEW | 7 | `engine/ConfigurationFileReader.cpp:93-101`, `filters/IncludeFilterFactory.cpp:59-67` | Medium | S | `Include:`가 없는 파일을 가리키면 로그에 `…: The operation completed successfully.`라고 찍힙니다. 실패 뒤에 읽는 `GetLastError()`가 0이기 때문이고, 실제 사유는 `Target::error`에 있습니다. 줄 번호도 붙지 않습니다. `[실행]` | TD-06과 함께 고칩니다. Include는 `LineOutcome::malformed`로 보고합니다 |
| TD-08 | NEW | 7 | `parser/RegexFunctions.cpp:50`, `:93`, `filters/IfFilterFactory.cpp:96-122`, `engine/FilterEngine.Configuration.cpp:217-236` | Medium | S | `If:`의 조건이 `ParserError`가 아닌 예외를 던지면 그 줄만 버려지고 본문이 실행됩니다. `If: regexSearch("[", "abc") != ""` 뒤의 `Preamp: -40 dB`가 적용됐고(출력 -45 dB, 대조군 -5 dB) `EndIf`는 "EndIf without an If"로 보고됐습니다. `[실행]` | 정규식 함수 안에서 `std::exception`을 잡아 `ParserError`로 다시 던집니다(`parser/RegistryFunctions.cpp:56-59`가 같은 방식입니다). `ParserTests`에 잘못된 패턴 사례를 넣습니다 |
| TD-09 | NEW | 8 | `engine/ConfigSwapChannel.h:57-61`, `:90-97`, `engine/FilterEngine.cpp:104-107`, `:155-158`, `engine/FilterEngine.Configuration.cpp:73-76` | Medium | M | 살아 있는 엔진에 `initialize()`를 다시 부르면 게시 권한이 두 생산자에게 나갑니다. `reset()`이 `permitHeld` 하나만 보고 권한을 놓기 때문에, 대기하던 로더가 받은 권한을 두 번째 `reset()`이 다시 풉니다. 확인용 테스트에서 `process()`를 한 번도 부르지 않았는데 재초기화와 대기 중이던 `loadConfig`가 둘 다 게시를 마쳤습니다. 두 번째 게시는 전환 중인 설정을 지울 수 있습니다(이 부분은 코드로만 확인). `[실행]` | `reset()`이 게시된 채 소비되지 않은 칸(`pendingReady`)이 있을 때만 권한을 놓게 합니다. 대기 중인 로더와 재초기화를 겹치는 사례를 `EngineLifecycleTests`에 넣습니다 |
| TD-10 | NEW | 8 | `engine/FilterEngine.Process.cpp:207-264`, `engine/FilterConfiguration.cpp:44-51`, `VoicemeeterClient/VoicemeeterClient.cpp:174-176`, `:215` | Medium | S | 엔진 입구에 `frameCount <= maxFrameCount` 검사가 없습니다. 저장소는 `maxFrameCount` 크기로 잡혀 있어 호스트가 알린 것보다 큰 블록을 넘기면 필터의 가드보다 먼저 버퍼 밖에 씁니다. 필터마다 대응도 다릅니다(건너뛰기, 음소거, 나눠 처리, 아무것도 안 함). `[코드]` | `processImpl` 입구에서 `maxFrameCount`씩 나눠 처리하거나 통과시키고, 계약을 `engine/IFilter.h`에 적습니다 |
| TD-11 | NEW | 3 | `filters/IIRFilter.cpp:35-56`, `filters/PreampCommand.cpp:38-44`, `filters/GraphicEQCommand.cpp:63-70`, `filters/BiQuadFilter.cpp:70-91` | Medium | M | 유한하지 않은 출력은 BiQuad만 막습니다. `Filter: ON IIR Order 1 Coefficients 1 0 1 -2`와 `Preamp: 99999 dB`는 `Max output level: inf`를 냈고 로그는 없습니다. IIR은 다음 reload까지 상태에 남습니다. `[실행]` | 선형 이득의 범위를 파싱 때 검사하고, IIR에 블록마다 유한성 검사와 상태 초기화를 둡니다 |
| TD-12 | NEW | 7 | `services/logging/Logging.cpp:98`, `:118-131`, `platform/windows/Win32Error.cpp:21-30` | Medium | S | 로그 파일을 `L"at"`로 열고 `fwprintf`로 쓰므로 현재 로캘로 바꿀 수 없는 글자가 `?`가 됩니다. `Include: 없는파일.txt`의 로그가 `????.txt`로 나왔습니다(같은 쓰기 코드를 지나는 stderr로 확인했고, 파일 쪽은 코드로 확인). 레지스트리 오류 문구에는 숫자 코드도 없습니다. `[실행]` | `ccs=UTF-8`로 엽니다(`services/diagnostics/InstallDiagnostics.cpp:346`이 이미 그렇게 합니다). 오류 문구 뒤에 숫자 코드를 붙입니다 |
| TD-13 | NEW | 7 | `Editor/main.cpp:239-247`, `services/install/ApoRegistration.cpp:273`, `:303-326` | Medium | S | `--veloapp-updated` 훅이 AudioSrv를 멈춘 뒤 설치와 마이그레이션을 하고 마지막 줄에서만 다시 켭니다. 그 사이에서 예외가 나면 서비스가 멈춘 채 끝납니다. 제거 훅에는 같은 이유로 `SCOPE_EXIT`가 이미 있습니다. `[코드]` | 업데이트 훅에도 범위 가드를 둡니다. Editor가 시작할 때 AudioSrv가 멈춰 있으면 알립니다 |
| TD-14 | NEW | 6 | `DeviceSelector/DeviceTestDialog.cpp:93`, `:157-173`, `DeviceSelector/DeviceSelector.cpp:449-451` | Medium | S | 장치 테스트 대화상자에서 Esc를 누르면 `reject()`로 끝나 `closeEvent`를 거치지 않습니다. 대화상자가 파괴되면서 실행 중인 자식 `QThread`가 함께 파괴됩니다. 그 스레드는 오디오 서비스를 재시작하거나 `reinstall()`을 하는 중일 수 있습니다. `[코드]` | `reject()`와 `done()`이 `closeEvent`와 같은 대기를 하게 합니다 |
| TD-15 | NEW | 8 | `DeviceSelector/ResidentHostLauncher.cpp:28-47`, `devices/VoicemeeterAPOInfo.cpp:355-390`, `services/security/AudioEngineAccess.cpp:254-268` | Medium | M | 승격된 DeviceSelector가 띄운 상주 호스트와 Voicemeeter 클라이언트가 관리자 토큰을 물려받은 채 로그오프까지 남습니다. 둘 다 `FilterEngine`을 만들고, 설정 폴더는 Users가 고칠 수 있으므로 `VSTPlugin: Library …` 줄 하나로 승격된 프로세스에 DLL이 올라갑니다. 주석(`ResidentHostLauncher.cpp:39-41`)은 승격 상속만 적고 이 결과는 적지 않았습니다. `[코드]` | 두 보조 프로세스를 승격되지 않은 셸 토큰으로 띄우거나, 승격된 채로는 플러그인을 올리지 않게 합니다 |
| TD-16 | NEW | 8 | `asio/AsioRegistration.cpp:25`, `:215-234`, `services/install/ApoRegistration.cpp:498-506`, `services/shell/StartMenuShortcuts.cpp:50-93`, `platform/qt/QtAppBootstrap.cpp:74-82` | Medium | L | 기기 전체에 걸리는 등록이 설치한 사용자의 프로필 안 파일을 가리킵니다. HKLM `Run` 값(모든 사용자의 로그온 때 실행), audiodg가 올리는 COM 서버, 모든 사용자용 시작 메뉴의 DeviceSelector 바로 가기가 그렇습니다. 승격된 DeviceSelector의 Qt 플러그인 검색도 `addLibraryPath`라서 `QT_PLUGIN_PATH`가 그대로 유효합니다. 이 선택을 받아들인다는 기록은 찾지 못했습니다. `[코드]` | 결정을 문서로 남깁니다. 작게는 `Run` 값을 HKCU로 옮기고 DeviceSelector에서 `setLibraryPaths`를 씁니다 |
| TD-17 | NEW | 8 | `services/security/AudioEngineAccess.cpp:170-187`, `Editor/import/LegacyMigration.cpp:256-262`, `devices/DeviceAPOInfo.Change.cpp:208-226` | Medium | M | 앱 안 업데이트는 훅을 승격된 채 시작하므로, 매번 Users가 고칠 수 있는 설정 폴더에 대해 승격된 `icacls /grant … /T`를 돌리고 견본 파일을 복사합니다. 승격된 DeviceSelector도 같은 폴더에 고정된 이름의 `.reg` 백업을 씁니다. 헤더는 "File work is forbidden when elevated"라고 적었습니다(`Editor/import/LegacyMigration.h:67`). `icacls /T`가 그 안의 junction을 따라가는지는 확인하지 못했습니다. `[코드]` | 승격된 갈래에서는 권한이 이미 있으면 건너뛰고, 필요하면 `ConfigDirectoryHandles.h`의 재분석 지점 검사를 거쳐 핸들로 부여합니다 |
| TD-18 | NEW | 7 | `services/install/ApoRegistration.cpp:266-279`, `services/install/ApoRegistration.h:36-37`, `Editor/MainWindow.cpp:290-368` | Medium | S | 설치 훅에서 접근 권한 부여가 실패해도 로그만 남기고 `Success`를 돌려줍니다. `AclFailed`와 `ServiceFailed`는 어디서도 돌려주지 않습니다. Editor의 시작 점검에는 audiodg가 설치 폴더를 읽을 수 있는지가 없고 `--diagnose`에만 있습니다. `[코드]` | `doChecks`에 `isReadableByAudioEngine(InstallPath, ConfigPath)`를 넣고 실패하면 DeviceSelector로 안내합니다 |
| TD-19 | NEW | 2 | `Editor/widgets/cards/VSTCardEditor.cpp:528-548`, `filters/VSTPluginFilter.cpp:46-55`, `:125-146`, `:174-175` | Medium | M | 카드의 버스 판정은 계약이 없어도 `negotiateBusLayouts(Auto, Auto)`를 부르고 주석에는 "The same call the engine makes"라고 적었습니다. 엔진은 계약이 없으면 `negotiateChannelCount`를 부르고 업믹서 하위 분류면 스테레오 입력을 제안합니다. 계약 없이 올린 업믹서는 카드에 대칭 배치로, 재생에서는 스테레오 입력으로 나옵니다. `[코드]` | 협상 순서를 `vst/`의 함수 하나로 옮겨 둘이 같이 씁니다(`ARCHITECTURE_AUDIT.md` 후보 1) |
| TD-20 | NEW | 7 | `Editor/widgets/cards/VSTPluginSession.cpp:372-378`, `Editor/guis/VSTPluginFilterGUIDialog.cpp:43`, `Editor/widgets/cards/VSTPluginSession.cpp:573-594` | Medium | S | 패널을 대화상자로 여는 길은 `writeToEffect`와 `startEditing`을 SEH 가드 없이 부릅니다. 카드에 끼워 여는 길은 같은 두 호출을 `__try`로 감쌉니다. 패널을 열다 죽는 플러그인은 한쪽에서만 Editor를 끕니다. `[코드]` | 두 길이 가드된 함수 하나를 쓰게 합니다 |
| TD-21 | NEW | 3 | `filters/VSTPluginFilter.cpp:37`, `:196-198`, `Editor/helpers/PanelFeedEngine.cpp:291-294`, `:111-112`, `Tests/VstPreviewProbe/VstPreviewProbe.cpp:202-203` | Medium | S | 플러그인이 알려 주는 채널 수의 상한(1024)과 지연의 상한은 엔진 필터에만 있습니다. Editor의 패널 미리듣기는 0 이하만 거르고 채널마다 8192프레임 버퍼를 잡습니다. `[코드]` | 상한 검사를 `VSTPluginInstance` 쪽으로 옮겨 모든 호출자가 같은 값을 받게 합니다 |
| TD-22 | NEW | 4 | `EqualizerAPOHost/main.cpp:72-92`, `:112-125`, `Tests/AsioTests/DaemonTests.cpp:746-812`, `asio/StreamProcessor.h:132` | Medium | M | 호스트가 ring에 붙는 코드가 exe의 `main.cpp`에 있어 테스트는 사본(`ControlRingHost`)을 씁니다. 사본은 ring을 못 열면 `BadRing`을 회신하지만 제품은 항상 `Accepted`를 회신하고 작업 스레드에서 로그만 남깁니다. DAW는 `readyTimeoutMs`(20초)를 기다립니다. `[코드]` | `StreamHost`를 `asio/`로 옮기고 ring을 요청 처리 중에 열어 실패를 바로 회신합니다(`ARCHITECTURE_AUDIT.md` 후보 4) |
| TD-23 | NEW | 5 | `.github/workflows/biweekly-audit.yml:248-254`, `.github/audit-pathspecs.txt` | Medium | S | 격주 감사의 변경 요약이 Windows job에서 항상 빕니다. 저장소에 `.gitattributes`가 없고 러너가 `core.autocrlf=true`라 pathspec 파일이 CRLF로 풀리고, `mapfile`이 `\r`이 붙은 pathspec을 만듭니다. 같은 명령이 그대로는 0건, `\r`을 떼면 681개 파일을 냅니다. `2>/dev/null \|\| true`가 가립니다. `[실행]` | `mapfile -t … < <(tr -d '\r' < 파일)`로 읽거나 `.gitattributes`에 `eol=lf`를 줍니다 |
| TD-24 | NEW | 5 | `common.pri:8-23`, `Editor/Editor.pro:27-35`, `DeviceSelector/DeviceSelector.pro:13-17`, `VoicemeeterClient/VoicemeeterClient.vcxproj:135-152` | Medium | S | qmake로 빌드하는 Editor와 DeviceSelector에 `/guard:cf`, `/sdl`, `/WX`가 없습니다. 같은 엔진 소스가 `Common.vcxproj`에서는 셋 다 받습니다. DeviceSelector는 관리자 권한으로 돌고 Editor는 승격된 훅을 돕니다. 배포되는 VoicemeeterClient에도 CFG와 SDL이 없습니다. `[코드]` | `common.pri`에 `QMAKE_CXXFLAGS += /guard:cf /sdl`, `QMAKE_LFLAGS += /guard:cf`와 `/WX`를 넣고, VoicemeeterClient에 두 속성을 넣습니다 |
| TD-25 | NEW | 4 | `Installer/AutoInstaller.cpp:196-490`, `:697-699`, `Tests/EditorLogicTests/AutoInstallerLogicTests.cpp:29-125` | Medium | S | README가 권하는 다운로드인 `EqualizerAPO-XT-Setup.exe`를 CI가 빌드만 하고 실행하지 않습니다. 테스트는 채널 고르는 로직과 UI 모델만 다루고, 다운로드와 해시 대조와 실행은 다루지 않습니다. `--detect-only` 플래그가 있는데 쓰는 스크립트가 없습니다. `[코드]` | avx2 leg에서 `--detect-only --out`을 돌려 채널이 매니페스트에 있는지 확인합니다. 다운로드와 검증은 가짜 전송으로 테스트할 수 있게 떼어 냅니다 |
| TD-26 | NEW | 4 | `EqualizerAPO/EqualizerAPO.cpp:223-268`, `:416-428`, `:497-509`, `.github/scripts/Invoke-MemcheckTests.ps1:64-65` | Medium | M | APO의 COM 껍데기는 어느 스위트에도 없습니다. 자식 APO를 만들고 잇는 흐름과 그 실패 갈래는 자동 테스트가 없고, 게이트의 VB-CABLE에는 제조사 효과 사슬이 없습니다. memcheck는 스위트만 다시 빌드하므로 DLL은 ASan 아래에서 돌지 않습니다. `[보고]` | 가짜 자식 APO와 `ApoHostProbe --child-clsid` 한 판을 더합니다 |
| TD-27 | NEW | 4 | `engine/FilterEngine.cpp:194-197`, `engine/FilterEngine.Runtime.cpp:37-78`, `Tests/EngineOrchestrationTests/EngineOrchestrationTests.cpp:90` | Medium | S | 엔진의 감시 스레드는 `customPath`가 비어 있을 때만 도는데 모든 테스트가 `customPath`를 줍니다. '오디오가 흐르는 동안 config.txt를 고친다'는 흐름이 테스트에서 한 번도 돌지 않습니다. TD-09가 그 자리에서 나왔습니다. `[코드]` | `FakeRegistry`로 ConfigPath를 주고, 다른 스레드에서 `process()`를 돌리며 파일을 여러 번 고쳐 쓰는 사례를 넣습니다 |
| TD-28 | NEW | 4 | `filters/VSTPluginFilter.cpp:403-421`, `:436-570`, `Tests/TestVst2Plugin/TestVst2Plugin.cpp:665-683`, `Tests/TestVst3Plugin/TestVst3Plugin.cpp:1291-1309` | Medium | S | 죽는 플러그인이 audiodg를 끌고 가지 않게 하는 SEH 가드가 실행된 적이 없습니다. 테스트 플러그인은 엉뚱한 값은 돌려주지만 접근 위반을 일으키는 모드는 없습니다. `[보고]` | process, prepare, dispatcher에서 죽는 모드를 더하고 통과 출력과 미뤄진 로그 줄을 확인합니다 |
| TD-29 | NEW | 4 | `Tests/TestHarness.h:406-417`, `.github/workflows/build.yml:329-338`, `:762-834` | Medium | S | 스위트를 돌리는 단계에 `timeout-minutes`가 없고(memcheck job 전체에 0건) 하네스는 끝날 때까지 사례 이름을 찍지 않습니다. 멈추면 job 한도까지 가고 어디서 멈췄는지 알 수 없습니다. 이번 감사에서도 확인용 테스트 하나가 멈췄을 때 출력이 전혀 없었습니다. `[실행]` | `runAll`이 사례 이름을 먼저 찍게 하고, 스크립트가 스위트마다 기한을 두고 돌립니다 |
| TD-30 | NEW | 7 | `devices/DeviceAPOInfo.cpp:78-82`, `devices/VoicemeeterAPOInfo.cpp:59-62`, `Editor/MainWindow.cpp:89-90` | Medium | S | `loadAllInfos`는 엔드포인트마다 예외를 잡지만 Voicemeeter와 ASIO를 덧붙이는 두 줄은 그 밖에 있습니다. Voicemeeter 제거 키에 `UninstallString`이 없으면 `RegistryError`가 나가고, Editor 생성자는 잡지 않아 시작하다 종료합니다. `[코드]` | 두 호출도 각각 감쌉니다. 제거 sweep은 이미 그렇게 합니다(`services/install/ApoRegistration.cpp:413-416`) |
| TD-31 | NEW | 9 | `CLAUDE.md:94`, `:115-116` | Medium | S | `CLAUDE.md`의 빌드 절차 두 군데가 그대로 따라 하면 다음 단계를 깹니다. `$env:LIB = "…"`는 VS 환경의 `LIB`를 덮어쓰고(`Build-Solution.ps1:49-50`은 앞에 붙입니다), Qt 절차는 상대 경로를 `PATH`에 넣은 뒤 `cd`합니다. 에이전트는 이 파일을 글자 그대로 실행합니다. `[코드]` | `LIB`는 앞에 붙이고 Qt 경로는 절대 경로로 적습니다. 더 줄이면 `setup-build.ps1`이 찍어 주는 두 호출만 남깁니다 |
| TD-32 | NEW | 1 | `vst/VSTPluginInstance.h:34-177`, `filters/VSTPluginFilter.cpp:487-507`, `Editor/helpers/PanelFeedEngine.cpp:166-190`, `:299-304`, `Tests/VstPreviewProbe/VstPreviewProbe.cpp:120-144` | Medium | L | 플러그인 인스턴스를 구동하는 순서(협상, 준비, 상태 쓰기, 시작, 처리 폭 고르기)가 엔진, 패널 미리듣기, 프로브에 한 벌씩 있고 테스트의 `create` 23곳이 손으로 따라 합니다. TD-19, TD-20, TD-21이 여기서 나옵니다. `[코드]` | `ARCHITECTURE_AUDIT.md` 후보 1 |
| TD-33 | NEW | 1 | `Editor/main.cpp:442-500`, `:535-540`, `:553-659`, `Editor/SkinGallery.h:27-160` | Medium | M | `main()`이 419줄이고 그 대부분이 게이트와 프로브를 가르는 분기입니다. 2026-08-15 이후 이 파일을 고친 커밋 28건 중 14건이 그 줄을 고쳤습니다. 플래그 다섯 개가 두 번 적혀 있습니다. `[코드]` | `ARCHITECTURE_AUDIT.md` 후보 3 |
| TD-34 | NEW | 3 | `DeviceSelector/DeviceSelector.cpp:211`, `:387-405`, `:685`, `:830`, `Editor/MainWindowParts/MainWindow.Device.cpp:195`, `Editor/guis/DeviceFilterGUI.cpp:99`, `Editor/guis/DeviceFilterGUIDialog.cpp:62` | Medium | M | 장치 종류와 상태를 화면용 문자열로 가립니다(`getTransportLabel() == L"ASIO"` 두 곳, `stateNote() == L"Voicemeeter was uninstalled"` 네 곳). 같은 질문에 답하는 `isVoicemeeterInstalled()`는 부르는 곳이 없습니다. 변경을 적용하는 순서도 호출자마다 다릅니다. `[코드]` | `AbstractAPOInfo`가 타입 있는 값으로 답하게 합니다(`ARCHITECTURE_AUDIT.md` 후보 6) |
| TD-35 | NEW | 2 | `.github/workflows/build.yml:246-325`, `:775-823`, `:1061-1121`, `.github/workflows/vst3-preview-probe.yml:132-160`, `setup-build.ps1:54-116`, `.github/scripts/Build-Solution.ps1:37`, `:52-67` | Medium | M | 작업 공간을 준비하는 순서가 YAML 네 군데와 `setup-build.ps1`에 한 벌씩 있고 서로 다릅니다. 기본 variant는 매니페스트의 `Primary` 대신 `"avx2"`라는 글자로 여러 곳에 적혀 있습니다. `setup-build.ps1`이 만든 배치에서 `Build-Solution.ps1`을 부르면 `QT_ROOT` 없이는 Qt를 찾지 못했습니다. `[실행]` | `ARCHITECTURE_AUDIT.md` 후보 5 |
| TD-36 | NEW | 7 | `vst/VST3Instance.State.cpp:137-158`, `vst/VST2Instance.cpp:607-612`, `filters/VSTPluginFilter.cpp:414` | Medium | S | 플러그인 상태 복원이 실패해도 아무것도 남지 않습니다. base64 디코드 실패는 통째로 건너뛰고 `setState`와 `setComponentState`의 결과는 버립니다. 잘린 `ChunkData`를 가진 줄은 audiodg에서 공장 기본값으로 돌고 로그가 없습니다. `[코드]` | `writeToEffect`가 성공 여부를 돌려주고, 디코드 실패와 거절을 라이브러리 경로와 함께 로그에 남깁니다 |
| TD-37 | NEW | 4 | `Tests/EngineOrchestrationTests/ConfigurationFileReaderTests.cpp:73`, `:82`, `Tests/EditorLogicTests/ConfigFileCodecTests.cpp:94-95`, `Editor/ConfigFileCodec.cpp:35` | Medium | S | 릴리스를 막는 테스트에 벽시계 기준이 남아 있습니다. 취소된 읽기는 100ms 안에, 100ms 기한은 500ms 안에 끝나야 통과합니다. `303188c2`는 저장 재시도 테스트의 기한을 2초에서 15초로 늘렸지만 그 테스트가 기대는 것은 제품의 250ms 재시도 창이라 원인은 그대로입니다. `[코드]` | 시계를 주입하거나 재시도 횟수로 판정합니다 |
| TD-38 | NEW | 1 | `Editor/gallery/GallerySkinShots.cpp:450`, `SubwooferRoutingCore/src/Compiler.cpp:736`, `Editor/gallery/GalleryGates.cpp:89`, `:711`, `Editor/widgets/FilterCardRow.cpp:52`, `devices/DeviceAPOInfo.cpp:104`, `Editor/AnalysisThread.cpp:111` | Low | M | 150줄을 넘는 함수가 많습니다. `renderSkin` 753줄, `subroute::validate` 642줄, `runCardSelectionTest` 491줄, `runSwitchTest` 396줄, `FilterCardRow` 생성자 289줄, `loadFromRegistry` 249줄, `AnalysisThread::run` 246줄입니다. `FilterCardRow.cpp`는 큰 파일과 자주 고친 파일 양쪽 목록에 다 듭니다. `[실행]` | 고칠 일이 생긴 함수부터 단계별로 나눕니다. 스킨의 `paintAnalysisGraph`(275~400줄)는 디자인이라 대상이 아닙니다 |
| TD-39 | NEW | 1 | `services/registry/ClsidRegistration.cpp:9`, `asio/AsioRegistration.cpp:11-12`, `runtime/ipc/StreamRing.h:53`, `platform/qt/QtAppBootstrap.cpp:22-30`, `services/install/ApoRegistration.h:26` | Low | M | 폴더 사이에 include 순환이 있습니다. `services/registry`와 `devices`, `asio`와 `devices`, `runtime/ipc`와 `asio`, `platform`과 `services`가 서로를 포함합니다. `devices/DeviceAPOInfoKeys.h`에 장치와 무관한 이름(CLSID 키, 서비스 이름, 파이프 이름)이 함께 들어 있는 것이 원인 가운데 하나입니다. `[코드]` | 그 이름들을 `services/registry/RegistryPaths.h` 쪽으로 옮깁니다 |
| TD-40 | NEW | 1 | `Editor/widgets/cards/ConvolutionCardEditor.cpp:89-137`, `Editor/widgets/cards/MultiConvolutionCardEditor.cpp:231-279`, `Editor/guis/GraphicEQFilterGUI.cpp:303-410`, `Editor/widgets/cards/GraphicEQCardEditor.cpp:313-400` | Low | M | 두 convolution 카드의 49줄이 class 이름만 다르고 같습니다(`diff`로 확인). GraphicEQ의 CSV 가져오기, 내보내기, 뒤집기는 옛 GUI와 카드에 한 벌씩 있습니다. `docs/FilterListUiPolicy.md:63-64`는 두 세대가 함께 쓰는 로직을 한 곳에 두라고 정했습니다. `[실행]` | `ARCHITECTURE_AUDIT.md` 후보 7. GraphicEQ의 세 동작은 Qt 위젯을 모르는 함수로 빼서 둘이 씁니다 |
| TD-41 | NEW | 1 | `runtime/memory/AlignedMemory.h:67-80`, `:137-151`, `engine/IFilterFactory.h:79`, `services/update/VelopackBootstrap.h:54`, `:59`, `services/windows/WindowsService.h:41`, `devices/DeviceAPOInfo.h:99`, `asio/AsioRegistration.h:37`, `services/audio/AudioFormatProbe.h:50`, `DeviceSelector/DeviceTestThread.h:63`, `release/ReleaseAssetNames.h:39-42` | Low | S | 부르는 곳이 없는 공개 함수가 있습니다. `AlignedMemory`의 `constructUnique`, `UniqueObject`, `ObjectDeleter`, `destroy`, `IFilterFactory::initialize(FilterEngine&)`, `VelopackBootstrap::currentBinDir`와 `isRestartingAfterUpdate`, `WindowsService::getServiceName`, `DeviceAPOInfo::getDefaultDevice`, `AsioRegistration::entrySuffix`, `AudioFormatProbe::describe`, `nonWorkingDeviceCount`, `universalSetupAssetName`입니다. `[코드]` | 지웁니다 |
| TD-42 | NEW | 1 | `Editor/main.cpp:60-64`, `Editor/FilterTableParts/FilterTable.Model.cpp:42-55`, `Editor/MainWindowParts/MainWindow.FileIO.cpp:105-109` | Low | S | 쓰지 않는 include와 닿지 않는 갈래가 있습니다. `main.cpp`의 VST 헤더 다섯 개, `FilterTable.Model.cpp`의 GUI 팩토리 헤더 14개는 이름을 쓰는 곳이 없습니다. `save()`의 `bytesWritten != totalBytes` 갈래는 `writeConfig`가 커밋 뒤에만 `opened`를 참으로 하므로 닿지 않습니다. `[코드]` | 지웁니다 |
| TD-43 | NEW | 1 | `Editor/widgets/FilterCardModel.cpp:79-127`, `:254-263`, `:432-455`, `Editor/widgets/ChannelFlow.cpp:50-200` | Low | M | '이 줄에서 선택된 채널'을 구하는 계산이 둘입니다. `calculateScopes`는 자체 `parseChannelList`를 쓰고 Device, Stage, 인라인 수식, If 갈래를 구분하지 않아 `computeChannelFlow`와 답이 다를 수 있습니다. `[코드]` | `ARCHITECTURE_AUDIT.md` 후보 9 |
| TD-44 | NEW | 1 | `Editor/MainWindowParts/MainWindow.FileIO.cpp:89-145`, `Editor/MainWindowParts/MainWindow.Device.cpp:151-166`, `Editor/MainWindowParts/MainWindow.FileActions.cpp:89-93`, `:128-143` | Low | M | `save()`가 `bool`만 돌려주고 dirty 표시는 호출자 다섯 곳이 세 가지 방식으로 맞춥니다. 타이머 람다와 `flushPendingInstantSave`는 같은 순서의 사본입니다. 파일 경로는 `FilterTable`과 `DocumentState`에 둘 다 있습니다. `[코드]` | `ARCHITECTURE_AUDIT.md` 후보 10 |
| TD-45 | NEW | 1 | `filters/DeviceFilterFactory.cpp:44-76`, `filters/StageFilterFactory.cpp:46-99`, `filters/IfFilterFactory.cpp:234-262`, `filters/SendFilterFactory.cpp:224` | Low | M | '조건이 거짓이면 뒤따르는 줄을 삼킨다'가 세 팩토리에, Include 앞뒤의 저장과 복원이 다섯 군데에 있습니다. 삼킨 줄을 trace에 남기는 것은 If뿐입니다. 팩토리 상태가 `LoadSession` 밖이라 load가 되돌아갈 때 같이 되돌아가지 않고, Send는 load가 확정되기 전에 공유 ring에 내보냅니다. `[코드]` | `ARCHITECTURE_AUDIT.md` 후보 8 |
| TD-46 | NEW | 2 | `asio/Win32HostLink.cpp:54-95`, `DeviceSelector/ResidentHostLauncher.cpp:27-51`, `asio/AsioRegistration.cpp:221`, `:234`, `asio/StreamProcessor.h:133`, `asio/HostControlServer.h:44`, `:72`, `EqualizerAPOHost/main.cpp:37` | Low | S | 호스트 exe 이름이 세 파일에, 같은 인자의 `CreateProcessW`가 두 곳에, `--resident` 문자열이 두 곳에 있습니다. 기본 linger 60000은 네 번 적혀 있습니다. `[코드]` | `asio/HostLaunch.h` 하나에 모읍니다 |
| TD-47 | NEW | 2 | `asio/WasapiExclusiveTarget.cpp:220`, `devices/EndpointProbe.cpp:81`, `EqualizerAPOHost/Keepalive.cpp:87`, `services/install/ApoRegistration.cpp:176`, `Editor/import/LegacyMigration.cpp:169-172` | Low | S | 같은 사실이 여러 곳에 있습니다. 엔드포인트 ID 접두가 세 번(네 번째 자리에서는 빠져 TD-05), 설치 폴더를 구하는 방법이 넷, `InstallPath`·`ConfigPath`·`EnableTrace` 값 이름이 상수 없이 흩어져 있고, 마이그레이션이 쓰는 값 셋을 지우는 목록은 다른 파일에 따로 있습니다. `[코드]` | 값 이름을 `RegistryPaths.h`의 상수로 두고, 쓰는 쪽과 지우는 쪽이 같은 목록을 읽게 합니다 |
| TD-48 | NEW | 2 | `filters/BiQuadCommand.cpp:148`, `filters/VSTPluginCommand.cpp:167`, `filters/DelayCommand.cpp:44`, `filters/GraphicEQCommand.cpp:39`, `filters/HilbertFilterFactory.cpp:23`, `filters/HilbertCommand.cpp:70` | Low | M | 코덱의 `parse` 모양이 일곱 가지이고 `false`의 뜻도 코덱마다 다릅니다('내 줄이 아님'과 '깨진 줄'이 섞임). '내 줄이 아님'은 팩토리 11곳이 `LineOutcome::notMine()`, 나머지가 `return {};`로 씁니다. 여섯 팩토리는 키워드를 검사한 뒤 같은 검사를 하는 `parse`를 다시 부릅니다. `[보고]` | 새 코덱부터 `parse(command, parameters, out, error)` 하나로 맞추고, `notMine()` 한 가지로 씁니다. 코덱 패턴 자체는 그대로 둡니다 |
| TD-49 | NEW | 2 | `Editor/skins/matrix/MatrixSkin.CommandRows.cpp:31`, `Editor/guis/ExpressionFilterGUIFactory.cpp:28`, `Editor/widgets/ChannelFlow.cpp:39-47`, `Editor/widgets/FilterCardModel.cpp:180-191`, `Editor/skins/studio/StudioSkin.Analysis.cpp:190` | Low | M | Editor 안의 작은 반복입니다. 행 종류를 문자열로 비교하는 자리가 66곳, '인라인 수식이 있는가'의 구현이 셋(그중 하나는 `contains('`')`), `repolish()` 도우미 옆의 손 `unpolish` 15곳, 스킨 여섯이 되풀이하는 `qBound(plotRect.top, zeroY, plotRect.bottom)` 12곳입니다. `[코드]` | 행 종류를 enum으로, 수식 판정을 함수 하나로, 영점 위치를 상태 값의 필드로 둡니다 |
| TD-50 | NEW | 3 | `Editor/MainWindowParts/MainWindow.Preferences.cpp:81-120`, `:171-197`, `DeviceSelector/DeviceSelector.cpp:577`, `:730`, `services/settings/EditorSettings.h` | Low | M | 설정 키가 문자열로 흩어져 있습니다(이름 붙은 키 8개 옆에 날 문자열 키 16개, 각각 읽는 곳과 쓰는 곳에 한 번씩). 선택한 장치는 GUID 대신 표시 이름으로 저장해 Windows에서 장치 이름을 바꾸면 선택이 풀립니다. 콤보 인덱스를 `InstallMode`로 바로 캐스팅하는데 `.ui`의 순서와 enum을 묶는 검사가 없습니다. `[보고]` | 키를 `EditorSettings::Keys`로 옮기고, 장치는 GUID로 저장하며, 콤보 항목에 enum 값을 데이터로 답니다 |
| TD-51 | NEW | 3 | `vst/VSTPluginInstance.h:38`, `vst/AbstractLibrary.h:31-39`, `filters/VSTPluginCommand.cpp:167`, `filters/VSTPluginFilterFactory.cpp:55-56` | Low | S | 뜻이 여럿인 날 값이 interface를 건넙니다. `create(library, 1 또는 2)`의 숫자는 VST2 호스트 콜백의 답 코드이고 28곳에 적혀 있습니다. `AbstractLibrary::initialize`의 `int`는 뜻이 셋입니다. `VSTPluginCommand::parse`는 `configPath`를 받고 쓰지 않으며, 팩토리는 빈 `configPath`를 '바이너리를 올리지 않는 모드'로 씁니다. `[코드]` | enum과 이름 있는 옵션으로 바꿉니다 |
| TD-52 | NEW | 8 | `vst/VST2Instance.cpp:618-621`, `:648-652`, `vst/AbstractLibrary.cpp:31-36`, `vst/VST3Instance.cpp:516-519`, `asio/WasapiExclusiveTarget.cpp:651-655`, `asio/WaveFormat.h:29-39`, `Benchmark/Benchmark.cpp:115` | Low | S | 남이 준 데이터를 길이나 초기화 없이 읽는 자리가 있습니다. `char buf[256]`을 초기화하지 않고 `effGetParamName`에 넘긴 뒤 읽고, `PClassInfo`의 이름을 끝 표시 없이 읽을 수 있으며, EXTENSIBLE 형식 blob은 `sizeof(WAVEFORMATEX)`만 확인하고 확장 필드를 읽습니다. Benchmark는 `SF_INFO`를 0으로 채우지 않습니다. `[코드]` | `= {}`로 초기화하고, 길이를 받는 복사 도우미를 쓰며, blob 크기를 `WAVEFORMATEXTENSIBLE`과 비교합니다 |
| TD-53 | NEW | 8 | `runtime/ipc/StreamRing.cpp:168-172`, `asio/DaemonProcessor.cpp:117`, `:157`, `:189` | Low | S | ring 생산자가 블록마다 공유 헤더에서 slot 오프셋을 다시 읽습니다. 소비자는 처음에 한 번 복사합니다. 호스트 쪽에서 헤더가 깨지면 DAW 프로세스가 엉뚱한 주소를 읽고 씁니다. `[코드]` | 생산자도 생성 때 오프셋을 복사해 둡니다 |
| TD-54 | NEW | 8 | `asio/Win32HostLink.cpp:169`, `runtime/ipc/SendRingWin32.cpp:19` | Low | S | 이름 있는 파일 매핑을 만들 때 `ERROR_ALREADY_EXISTS`를 보지 않습니다. 파이프 쪽은 선점을 막고 테스트까지 있는데(`Tests/AsioTests/DaemonTests.cpp:627`) 매핑은 누가 먼저 만든 것을 그대로 엽니다. `[코드]` | Win32HostLink는 이미 있으면 실패로 처리합니다. SendRing은 여럿이 같이 여는 설계라면 그 사실을 자리에 적습니다 |
| TD-55 | NEW | 8 | `EqualizerAPO/EqualizerAPO.cpp:536-557`, `platform/windows/NamedPipeSecurity.h:59-65`, `DeviceSelector/DeviceTestThread.cpp:114`, `:122-132` | Low | S | audiodg가 장치 테스트 파이프에 붙을 때 서버를 확인하지 않고 바로 쓰고, 기한 없는 `FlushFileBuffers`로 기다립니다. 파이프 이름을 담은 레지스트리 값은 DeviceSelector가 비정상 종료하면 남습니다. `[보고]` | `SECURITY_SQOS_PRESENT \| SECURITY_IDENTIFICATION`으로 열고 flush를 빼거나 기한을 둡니다 |
| TD-56 | NEW | 8 | `Installer/AutoInstaller.cpp:173-183`, `:427-441`, `:474-490`, `:250-255` | Low | S | 설치기가 파일을 검증한 뒤 경로로 실행하고, 체크섬 파일도 예측 가능한 임시 파일로 썼다가 읽습니다. 그 사이에 같은 사용자의 다른 프로세스가 바꿀 수 있습니다. 200이 아닌 응답은 전부 '없음'으로 보고합니다. `[코드]` | 해시부터 `CreateProcess`까지 쓰기 금지 핸들을 쥐고, 체크섬은 메모리에 둡니다 |
| TD-57 | NEW | 6 | `filters/IrCache.cpp:153-169`, `:185-201`, `engine/ConfigurationFileReader.cpp:26-43` | Low | M | IR 파일과 설정 파일에 크기 상한이 없습니다. IR은 헤더가 말하는 프레임 수만큼 먼저 할당하고 0으로 채운 뒤 읽으며 사본을 하나 더 만듭니다. 설정 파일은 통째로 문자열에 읽습니다. `[보고]` | 총 샘플 수를 제한하고 조각으로 읽습니다 |
| TD-58 | NEW | 6 | `Editor/helpers/PluginLoadQueue.cpp:140-152`, `Editor/AnalysisThread.cpp:41-50`, `DeviceSelector/ReceiveThread.cpp:39-54`, `Editor/main.cpp:442-500`, `:686-696` | Low | M | 종료 때 기한 없이 기다리는 자리가 있습니다(플러그인 로더 join, 분석 스레드 `wait()`, 수신 스레드 join). 반대로 헤드리스 게이트 14곳은 `main()`의 종료 순서를 건너뛰고 바로 `return`하며 각자 `waitForPendingLoads`를 부릅니다. `[코드]` | 기다림에 기한을 두고, 게이트의 종료는 한 곳에서 합니다 |
| TD-59 | NEW | 6 | `Editor/widgets/cards/FileReferenceController.cpp:117-137`, `Editor/helpers/VstChunkScan.cpp:32-45` | Low | M | 파일을 가리키는 카드가 UI 스레드에서 파일 존재와 AuthZ 검사를 합니다. 끊긴 네트워크 드라이브를 가리키는 줄이 있으면 행을 만들 때마다 시간 초과만큼 멈춥니다. `[보고]` | 엔드포인트 probe와 같은 `BackgroundProbe`로 옮깁니다 |
| TD-60 | NEW | 6 | `Editor/main.cpp:312`, `DeviceSelector/main.cpp:452`, `services/logging/Logging.cpp:95-136` | Low | S | Editor와 DeviceSelector의 로그가 trace를 켠 채 한도 없이 자랍니다. 분석 패널이 열려 있으면 편집할 때마다 여러 줄이 붙습니다. `[보고]` | 시작할 때 크기를 보고 자르거나 돌립니다 |
| TD-61 | NEW | 6 | `dsp/FftwPlanningPolicy.cpp:43-51`, `:80-87`, `filters/loudnessCorrection/LoudnessCorrectionFilter.cpp:72-80`, `:100-103`, `:157-161` | Low | S | 보조 기능이 조용히 꺼집니다. FFTW wisdom 경로를 ANSI로 읽어 코드 페이지로 나타낼 수 없는 프로필에서는 저장이 실패하고 매번 다시 측정합니다. LoudnessCorrection은 볼륨을 못 읽어도 로그가 없고 `State 0`에도 스레드와 COM 객체를 만듭니다. `[코드]` | 넓은 문자 경로를 쓰고 실패를 한 번 로그에 남깁니다 |
| TD-62 | NEW | 7 | `filters/VSTPluginFilter.cpp:158-159`, `filters/IrCache.cpp:138`, `:144`, `asio/EngineHostCore.cpp:237-242`, `asio/HostControlServer.cpp:86-90`, `asio/AsioWrapper.cpp:480-484`, `DeviceSelector/DeviceSelector.cpp:88-96`, `Editor/MainWindow.cpp:426-429` | Low | S | 엉뚱하거나 찾을 수 없는 진단이 있습니다. `initialize()`의 모든 실패를 "crashed during initialization"으로 적고, IR 읽기 오류와 샘플레이트 불일치에는 파일 이름이 없으며, `catch (...)` 세 곳은 로그가 없습니다. DeviceSelector는 복구 결과와 무관하게 "The value has been corrected"라고 하고, Editor는 `ShellExecute`의 모든 실패를 '관리자 승인 필요'로 보여 줍니다. `[코드]` | 실제 사유를 그대로 전하고 결과를 확인한 뒤 문구를 고릅니다 |
| TD-63 | NEW | 7 | `VoicemeeterClient/VoicemeeterClient.cpp:166-233`, `:414-420`, `Editor/helpers/CrashHandler.cpp:125-135`, `Editor/SkinManager.cpp:59` | Low | S | Voicemeeter 오디오 콜백에 예외 경계가 없어 `initialize`의 예외가 C 프레임을 지나 종료로 이어집니다. 크래시 보고의 '마지막 동작'은 스킨 적용 때만 갱신되고, 레지스트리를 고치는 DeviceSelector에는 크래시 핸들러가 없습니다. `[보고]` | 콜백을 `try`로 감싸고, breadcrumb을 파일 열기와 플러그인 로드에도 찍습니다 |
| TD-64 | NEW | 7 | `Editor/main.cpp:217-219`, `:693-708`, `services/update/UpdateSession.cpp:105-115` | Low | M | 업데이트 흐름의 두 가지입니다. 훅의 12초 기한이 승격 재실행 전에 시작돼 사용자가 UAC에 답하는 시간까지 포함합니다. 다운로드 중에 Editor를 닫으면 창 없는 프로세스가 다운로드를 마칠 때까지 남았다가 승격을 묻습니다. `docs/VelopackUpdates.md`는 다운로드가 종료를 막지 않는다고 적었습니다. `[보고]` | 기한을 승격된 자식에서 시작하고, 종료 때 다운로드를 취소할 수 있게 합니다 |
| TD-65 | NEW | 4 | `Tests/AsioTests/AsioTests.cpp:1624-1630`, `Tests/TestHarness.h:378-397`, `Tests/EngineOrchestrationTests/EngineRoutingTests.cpp:190-198`, `Tests/EngineOrchestrationTests/JudgedPathTests.cpp:299-306`, `Tests/HybridConvTests/ConfigPathPolicyTests.cpp:676-691` | Low | S | 테스트가 조용히 빠질 수 있습니다. `AsioTests`는 스위트 이름 다섯 개를 직접 넘기므로 새 스위트 이름을 쓴 사례는 실행되지 않고 알림도 없습니다. `testRealBrirCrossfeed`는 CI가 `EAPO_XT_BRIR_DIR`을 주지 않아 항상 건너뜁니다. 권한에 달린 경로 보안 검사들은 건너뛸 때 `printf`만 합니다. `[코드]` | 목록에 없는 등록 스위트가 있으면 실패하게 하고, 건너뛴 수를 결과 줄에 셉니다 |
| TD-66 | NEW | 4 | `Tests/HybridConvTests/Vst3HostTests.cpp:352-1247`, `Tests/HybridConvTests/VstHostTests.cpp:540-764`, `Tests/TestHarness.h:119-123`, `:185-190` | Low | M | `runVst3HostTests`가 `TEST_CASE` 하나에 895줄입니다. `require`가 실패하면 `std::exit(1)`이라 30개가 넘는 스위트를 가진 실행 파일 전체가 거기서 끝나고 뒤의 결과가 가려집니다. `[코드]` | 시나리오별로 나누고, `require`는 현재 사례만 끝내게 합니다 |
| TD-67 | NEW | 4 | `.github/scripts/Invoke-CaptureGate.ps1:487-548`, `:1107-1113`, `.github/scripts/Invoke-PanelInputGate.ps1:101`, `.github/scripts/Test-SkinModules.ps1:1` | Low | M | 게이트 스크립트의 합격·불합격 판정에는 테스트가 없습니다. Pester는 `-PlanOnly` 표의 글자만 확인합니다. `Test-SkinModules.ps1`(211줄)에는 Pester가 없습니다. 게이트가 실패하지 않게 되어도 초록으로 남습니다. `[보고]` | 판정 함수를 module로 빼고 가짜 측정값으로 실패 사례까지 테스트합니다 |
| TD-68 | NEW | 4 | `.github/scripts/ApoEndpointHarness.psm1:7-18`, `Tests/HybridConvTests/Vst3HostTests.cpp:50-61`, `Tests/HybridConvTests/VstHostTests.cpp:103-156`, `Tests/TestVst3Plugin/TestVst3Plugin.cpp:1291-1309`, `Tests/VstPreviewProbe/VstPreviewProbe.vcxproj:78` | Low | S | 테스트가 제품과 따로 든 사본들입니다. PowerShell 쪽 CLSID와 값 이름은 C++ 헤더와 대조되지 않습니다. base64 인코더가 테스트 파일 넷에 있고 VST2 chunk 배치는 손 복사입니다. VST3 테스트 플러그인의 모드 12개는 모듈 경로 전체의 부분 문자열로 정해집니다. Editor의 `VSTPluginSession`, `PluginLoadQueue`, `PanelPreviewFeeder`는 어느 테스트에도 컴파일되지 않습니다. `[보고]` | psm1의 글자를 헤더와 대조하는 lint를 두고, 모드 이름을 `TestVst3Protocol.h`의 상수로 옮겨 파일 이름으로만 고릅니다 |
| TD-69 | NEW | 5 | `.github/workflows/build.yml:676`, `:869-882`, `:959`, `.github/scripts/New-VelopackRelease.ps1:93`, `.github/scripts/Provisioning.psm1:672`, `.github/scripts/New-BuildMatrix.ps1:46` | Low | S | 해시나 커밋으로 고정되지 않은 받기가 있습니다. cppcheck는 태그로만 받고, `vpk`(모든 설치 파일을 만드는 도구)와 Pester는 버전으로만 받으며, pip에는 `--require-hashes`가 없습니다. `windows-11-arm`과 `ubuntu-latest`는 떠 있는 라벨입니다. `[코드]` | cppcheck는 받은 뒤 커밋을 확인하고(`Sync-PinnedCheckout`이 이미 그 일을 합니다), vpk와 Pester는 nupkg 해시로 고정합니다 |
| TD-70 | NEW | 5 | `analysis-layout/right-dock.png`, `release/ReleaseAssetNames.h`, `.gitignore:16`, `:285` | Low | S | 무시 규칙에 걸리는 자리에 추적 파일이 둘 있습니다. `analysis-layout/right-dock.png`는 게이트의 출력 경로라 CI가 실행마다 덮어씁니다. `release/`는 `[Rr]elease/`에 걸려 새 파일을 넣어도 `git add -f` 없이는 추적되지 않습니다. `[실행]` | PNG는 추적에서 빼고, `release/`는 무시 규칙에 예외를 둡니다 |
| TD-71 | NEW | 5 | `.github/workflows/build.yml:1325-1328`, `.github/scripts/Build-Solution.ps1:57`, `:80-84`, `Tests/EditorLogicTests/EditorLogicTests.vcxproj:57` | Low | S | 빌드 진입이 조금씩 다릅니다. 배포되는 설치기는 `BuildModel`을 거치지 않는 손 `msbuild` 줄로 빌드되고, PR에서는 다른 인자로 빌드됩니다. `Resolve-QtRoot`의 마지막 후보 `<root>\Qt`와 EditorLogicTests의 `..\..\Qt` 기본값은 실제 배치(`Qt\6.12.0\msvc2022_64`)와 맞지 않습니다. `[실행]` | 설치기도 `Get-EapoMsBuildArguments`로 빌드하고, 두 기본값을 매니페스트의 Qt 버전에서 만듭니다 |
| TD-72 | NEW | 5 | `Common.vcxproj:98-151`, `Common.vcxproj.filters`, `Editor/Editor.pro:631`, `DeviceSelector/DeviceSelector.pro:44`, `:100`, `Editor/Editor.pro:50-54` | Low | S | 프로젝트 파일의 찌꺼기입니다. 정적 라이브러리인 `Common.vcxproj`에 쓰이지 않는 `*_LIB` 기본값 블록이 있고 Debug 값은 다른 소비자와 다릅니다. `.filters`에는 소스 11개와 헤더 32개가 빠져 있습니다. DeviceSelector는 `Common.lib`에 든 `ComSelfRegistration.cpp`를 또 컴파일합니다. 설치 관련 소스 다섯 개는 프로젝트 넷에 손으로 적혀 있습니다. CI가 빌드하지 않는 Win32 구성이 프로젝트 15개에 있습니다. `[코드]` | `Common.vcxproj`의 라이브러리 블록을 지우고, 설치 소스 묶음을 `asio/*Sources.props`처럼 props 하나로 뺍니다 |
| TD-73 | NEW | 5 | `.github/workflows/build.yml:28-32` | Low | S | 워크플로 전체에 `actions: write`와 `packages: write`가 주어져 있습니다. 패키지를 올리는 단계는 없고, 받은 코드를 실행하는 cppcheck, pester, memcheck, 게이트 job도 이 권한을 받습니다. `[코드]` | 워크플로는 `contents: read`만 두고 job마다 필요한 권한을 줍니다 |
| TD-74 | NEW | 5 | `docs/EnvironmentVariables.md:20-21`, `:81-83`, `Tests/HybridConvTests/HybridConvTests.cpp:155-165`, `.github/scripts/Test-SourceSync.ps1:229-261` | Low | S | 환경 변수 문서와 lint입니다. `EAPO_XT_JUDGED_IR`은 문서에 없고, 두 줄은 지금의 위치와 다릅니다. lint는 `_wdupenv_s`, `GetEnvironmentVariableA`, `pathutil::environmentVariable`, 스크립트 쪽 읽기, '문서에만 있는 변수'를 보지 않습니다. `[보고]` | 변수를 더하고 lint의 읽기 함수 목록을 넓힙니다 |
| TD-75 | NEW | 1 | `Editor/widgets/cards/ConvolutionCardEditor.cpp:182`, `Editor/widgets/cards/MultiConvolutionCardEditor.cpp:345`, `Editor/widgets/cards/FileReferenceController.cpp:127`, `Editor/MainWindow.h:240-246`, `DeviceSelector/main.cpp:86-164` | Low | M | 테스트용 장치가 제품 class 안에 있습니다. 카드 파일 셋이 `EAPO_SKIN_GALLERY`를 읽어 동작을 바꾸고, `MainWindow`에 프로브용 hook 셋과 플래그가 있으며, 관리자 권한으로 도는 DeviceSelector가 스크린샷 하네스를 품고 있습니다. `[코드]` | 환경 변수 대신 주입할 수 있는 판단 함수로 바꾸고, 프로브 hook은 `MainWindow::Probe` 쪽으로 모읍니다 |
| TD-76 | NEW | 9 | `docs/ErrorHandlingPolicy.md:62-63`, `:86`, `docs/RefactoringBaseline.md:7-18`, `docs/TestSeams.md:78-97`, `docs/SimdBuildMatrix.md:71-72`, `docs/VelopackUpdates.md:16`, `docs/LocalDependencySetup.md:15-49`, `Agent.md:52`, `SECURITY.md:16` | Low | S | 문서가 없는 것을 가리키거나 지금과 다릅니다. `ParseReportingFactory`, `ConvolutionFilePath`, `UpdateInfoFormatter`, 업데이트 체커는 코드에 없습니다. `TestSeams.md`는 `TEST_CASE` 이전의 등록 방식과 지금은 없는 lint를 설명합니다. `SimdBuildMatrix.md`는 EditorLogicTests가 모든 leg에서 돈다고 하고, `VelopackUpdates.md`는 없는 기본 채널을 말하며, `LocalDependencySetup.md`의 절차로는 링크되는 트리가 나오지 않습니다. `Agent.md`는 ARM64가 `v143`을 쓴다고 합니다. `[코드]` | 각 문서를 지금의 이름과 절차로 고칩니다. 빌드 절차 문서는 `setup-build.ps1`과 그것이 찍는 두 호출로 줄입니다 |
| TD-77 | NEW | 9 | `services/install/ApoRegistration.cpp:360-361`, `Editor/widgets/cards/VSTCardEditor.cpp:530-531`, `Editor/guis/BiQuadFilterGUI.cpp:106-109`, `filters/BiQuadCommand.h:67`, `Editor/guis/MultiConvolutionFilterGUIFactory.cpp:44-46`, `platform/windows/ComSelfRegistration.h:26-29`, `Editor/FilterTable.h:139-142`, `filters/ConfigPathPolicy.cpp:823`, `.github/workflows/build.yml:223-226`, `:433-438`, `.github/simd-variants.psd1:26-28`, `Directory.Build.props:13-17`, `common.pri:7` | Low | S | 옆의 코드와 어긋나는 주석입니다. `loadAllInfos`가 목록 전체를 던진다는 주석(지금은 엔드포인트마다 잡습니다), "The same call the engine makes"(TD-19), BiQuad의 'sole serializer'(`AllPassCardEditor.cpp:265-285`가 두 번째입니다), 서로 반대인 두 주석(`FilterTable.cpp:436-441`과 MultiConvolution GUI 팩토리), 없어진 두 번째 호출자, 옛 행이 `item->text`를 직접 쓰는데 그러지 않는다는 주석(`FilterTableRow.cpp:151`), 없는 'missing-file diagnostics', `GITHUB_TOKEN`으로 민다는 주석(배포 키), 'six' 게이트(13개), `AutoInstaller.cpp`(lint는 `AutoInstallerLogic.cpp`), 라이브러리 경로가 각 vcxproj에 있다는 주석, 'three Qt applications'(둘)입니다. `[코드]` | 주석을 고칩니다 |
| TD-78 | NEW | 9 | `CHANGELOG.md:150`, `:866`, `.github/workflows/build.yml:113-135` | Low | S | CHANGELOG의 버전 절을 버전을 올릴 때, 곧 게이트보다 먼저 자릅니다. 그래서 게시되지 않은 버전의 날짜 붙은 절이 남습니다. `v2.61.5`와 `v2.54.12`는 이 체크아웃에 태그가 없는데 절이 있습니다(GitHub의 태그는 확인하지 못했습니다). `[코드]` | 두 버전을 '게시되지 않은 버전' 목록에 더하거나, 절을 릴리스가 성공한 뒤에 자릅니다 |
| TD-79 | NEW | 9 | `Editor/main.cpp:535-540`, `Editor/widgets/cards/ConvolutionCardEditor.cpp:138-141`, `Editor/widgets/cards/MultiConvolutionCardEditor.cpp:280-287`, `Directory.Build.props:54-66` | Low | S | 이유가 적히지 않은 차이들입니다. 실제 창을 쓰는 프로브 셋(`--skin-metrics-probe`, `--vst-panel-feed-test`, `--skin-switch-storm`)이 테스트 모드 목록에 없습니다. 두 convolution 카드가 장치 샘플레이트를 다른 곳에서 읽습니다. `/WX`가 MSBuild에만 걸리고 Qt 앱에는 왜 없는지 적혀 있지 않습니다. `[코드]` | 의도라면 그 자리에 이유를 적고 이 번호(`TECH_DEBT_AUDIT TD-79`)를 인용합니다. 의도가 아니면 맞춥니다 |
| TD-80 | NEW | 2 | `Editor/helpers/VstChunkScan.cpp:22`, `Editor/helpers/VstChunkState.cpp:102-105`, `vst/VSTChunkBase64.h:22-87`, `Editor/import/LegacyMigration.cpp:66-110`, `:530-534`, `services/logging/Logging.cpp:98`, `Editor/helpers/CrashHandler.cpp:136-140`, `services/registry/WindowsRegistry.cpp:444` | Low | M | 같은 일을 하는 방법이 여럿입니다. chunk의 base64를 Editor가 Qt로 두 번, 엄격함을 다르게 해서 풀고 `vst/`는 Win32로 풉니다. 파일을 덮어쓰며 복사하는 코드가 넷인데 원자적인 것은 `ConfigFileCodec` 하나이고 한 곳은 `remove`의 결과를 버립니다. 넓은 문자를 파일에 쓰는 방법이 다섯입니다. `[보고]` | chunk 디코드는 `VSTChunkBase64`를 쓰고, 복사는 TD-01에서 만든 안전한 함수 하나를 씁니다 |

---

## 고칠 것이 다섯뿐이라면

### 1. 가져오기와 마이그레이션이 원본을 지우지 않게 (TD-01, TD-02)

대상 경로가 원본과 같거나 설정 폴더 밖이면 복사를 하지 않아야 하고, 복사는 대상을 먼저 지우지 않아야 합니다.

```cpp
// Editor/import/ConfigDependencyScanner.cpp
QString relativeToRoot(const QString& targetAbs, const QString& rootDir)
{
    const QString rel = QDir(rootDir).relativeFilePath(targetAbs);
    // 드라이브가 다르면 Qt는 절대 경로를 그대로 돌려준다.
    if (rel.isEmpty() || rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel))
        return QString();
    return QDir::fromNativeSeparators(rel);
}

// Editor/import/ImportExecutor.cpp, 항목마다
const QString destAbs = QDir::cleanPath(QDir(configDir).absoluteFilePath(relNative));
if (QDir::isAbsolutePath(relNative) || !isInside(destAbs, configDir)) { /* 오류로 보고하고 continue */ }
if (sameFile(item.sourceAbsolute, destAbs)) { /* 이미 제자리: 권한만 고치고 continue */ }
const QString temp = destAbs + QStringLiteral(".eapo-import");
QFile::remove(temp);
if (!QFile::copy(item.sourceAbsolute, temp)) { /* 오류, 대상은 그대로 */ }
if (!MoveFileExW(temp, destAbs, MOVEFILE_REPLACE_EXISTING)) { /* 오류, temp 정리 */ }
```

`sameFile`은 두 경로를 열어 `GetFileInformationByHandle`의 볼륨 번호와 파일 번호를 비교합니다. `Tests/EditorLogicTests/ConfigImportTests.cpp`에 두 사례를 넣습니다. 하나는 `cfg/IR/room.wav`를 `cfg`로 가져와도 원본이 남는 것, 다른 하나는 다른 드라이브의 파일을 가리키는 설정을 `SourceFolderIsRoot`로 훑으면 그 항목이 경고와 함께 빠지는 것입니다.

### 2. 풀리지 않은 채널 이름이 상수가 되지 않게 (TD-03)

```cpp
// filters/CopyFilter.cpp, initialize 안
struct Summand { enum class Kind { Channel, Constant } kind; int channel; double factor; };
...
const int index = ChannelLayout::getChannelIndex(name, channelNames);
if (index < 0 && !summand.isConstant)
{
    engine.reportParseError(L"Copy", L"channel \"" + name + L"\" does not exist on this device; the term was dropped");
    continue;   // 항을 버린다. 무음이 DC보다 안전하다.
}
```

`Tests/EngineOrchestrationTests/EngineRoutingTests.cpp`에 스테레오 엔진에서 `Copy: L=C`의 출력이 0이고 파싱 오류가 한 건 붙는 사례를 넣습니다.

### 3. 엔진의 두 가지 메모리 안전 계약을 엔진이 지키게 (TD-09, TD-10)

```cpp
// engine/ConfigSwapChannel.h
void reset(Pointer seed = Pointer()) noexcept
{
    currentConfig = std::move(seed);
    pendingConfig = Pointer();
    previousConfig = Pointer();
    // 게시된 채 소비되지 않은 칸이 쥔 권한만 돌려준다.
    // 아직 게시하지 않은 생산자의 권한은 그 생산자의 것이다.
    if (pendingReady.exchange(false, std::memory_order_acq_rel))
        releasePublishPermit();
}

// engine/FilterEngine.Process.cpp, processImpl 입구
if (frameCount > maxFrameCount)
{
    oversizedBlocks.fetch_add(1, std::memory_order_relaxed);   // load 스레드가 나중에 로그로 남긴다
    IoTraits::bypass(output, input, realChannelCount, outputChannelCount, frameCount);
    return;
}
```

`reset()`을 고치기 전에 `EngineLifecycleTests`에 '초기 게시가 남은 채 로더 하나가 대기 중일 때 재초기화하면, `process()` 없이는 둘 중 하나만 게시한다'는 사례를 먼저 넣습니다. 이번 감사의 확인용 테스트가 그 사례의 실패를 보였습니다. 로더가 실패했을 때 권한을 돌려주는 흐름이 이 변경과 맞는지는 `FilterEngine.Configuration.cpp:137-154`에서 함께 확인해야 합니다.

### 4. 설정 줄의 실패를 줄 번호와 함께 말하게 (TD-06, TD-07, TD-08)

```cpp
// filters/ConfigFileReference.h
struct Opened
{
    JudgedPath path;                 // 열려 있을 때만 유효
    std::wstring problem;            // 비어 있지 않으면 실패
    explicit operator bool() const { return problem.empty(); }
};
// what: L"impulse response file", L"configuration file" ...
static Opened open(const std::wstring& configPath, const std::wstring& written, const wchar_t* what);

// 팩토리 다섯 곳
const auto file = ConfigFileReference::open(configPath, cmd.path, L"impulse response file");
if (!file)
    return LineOutcome::malformed(file.problem);   // 안 적음, 거부, 없음(Win32 사유 포함)

// parser/RegexFunctions.cpp, Eval 안
try { /* std::wregex 생성과 매칭 */ }
catch (const std::exception& e) { throw mup::ParserError(/* e.what()을 담아 */); }
```

`ConfigLoadTests`에 없는 IR, 없는 Include, 잘못된 정규식의 `If`가 각각 파싱 오류 한 건을 내고, `If`의 본문이 실행되지 않는 사례를 넣습니다.

### 5. 배포할 것을 게시 전에 한 번 실행하기 (TD-04)

`create-release`를 세 단계로 나눕니다.

```yaml
- name: Pack (게시하지 않음)
  run: ./.github/scripts/New-VelopackRelease.ps1 ... -NoPublish
- name: Smoke-test the packed primary channel
  run: |
    $setup = Get-ChildItem releases -Filter "*x64-avx2*-Setup.exe" | Select-Object -First 1
    Start-Process $setup.FullName -ArgumentList "--silent" -Wait
    & "$env:LOCALAPPDATA\EqualizerAPO-XT-x64-avx2\current\Editor.exe" --selftest-vst
    & "$env:LOCALAPPDATA\EqualizerAPO-XT-x64-avx2\Update.exe" --uninstall
    if ((Get-Service Audiosrv).Status -ne 'Running') { throw "AudioSrv was left stopped" }
    # 캡처 게이트의 Test-ApoEqClsid로 EQ CLSID가 남지 않았는지 확인
- name: Publish
  run: ./.github/scripts/New-VelopackRelease.ps1 ... -PublishOnly
```

설치 폴더 이름과 조용한 설치 인자는 `Install-ReleaseBuild.ps1`이 이미 쓰는 값을 따릅니다. 업데이트 훅까지 보려면 직전 릴리스를 먼저 설치한 뒤 이번 패키지를 적용하는 단계를 하나 더 둡니다.

---

## 빨리 고칠 수 있는 것

공수가 S이고 심각도가 Medium 이상인 것들입니다.

- [ ] TD-01 `relativeToRoot`가 절대 경로를 거절하고, 실행기가 원본과 같은 대상을 건너뜁니다.
- [ ] TD-02 같은 가드가 카드의 '가져오기'도 막는지 테스트로 확인합니다.
- [ ] TD-03 `Copy:`의 풀리지 않은 채널 이름을 버리고 보고합니다.
- [ ] TD-05 `AudioFormatProbe::probe`에 엔드포인트 ID 접두를 붙입니다.
- [ ] TD-06, TD-07 없는 파일을 파싱 오류로 보고합니다.
- [ ] TD-08 정규식 함수의 예외를 `ParserError`로 바꿉니다.
- [ ] TD-10 `processImpl` 입구에 `frameCount` 가드를 둡니다.
- [ ] TD-12 로그 파일을 `ccs=UTF-8`로 엽니다.
- [ ] TD-13 `--veloapp-updated` 훅에 `SCOPE_EXIT`를 둡니다.
- [ ] TD-14 `DeviceTestDialog`에 `reject()`와 `done()`을 더합니다.
- [ ] TD-18 `doChecks`에 audiodg의 읽기 권한 검사를 넣습니다.
- [ ] TD-20 대화상자로 여는 길에도 SEH 가드를 씁니다.
- [ ] TD-21 플러그인 채널 수의 상한을 패널 미리듣기에도 적용합니다.
- [ ] TD-23 pathspec 파일에서 `\r`을 떼고 읽습니다.
- [ ] TD-24 `common.pri`와 VoicemeeterClient에 보호 플래그를 넣습니다.
- [ ] TD-25 설치기의 `--detect-only`를 CI에서 한 번 돌립니다.
- [ ] TD-27 감시 스레드와 `process()`를 함께 돌리는 사례를 넣습니다.
- [ ] TD-28 죽는 테스트 플러그인 모드를 더합니다.
- [ ] TD-29 `runAll`이 사례 이름을 찍고, 스위트 실행에 기한을 둡니다.
- [ ] TD-30 Voicemeeter와 ASIO 덧붙이기를 `try`로 감쌉니다.
- [ ] TD-31 `CLAUDE.md`의 `LIB`와 Qt `PATH` 줄을 고칩니다.
- [ ] TD-36 플러그인 상태 복원의 실패를 로그에 남깁니다.
- [ ] TD-37 벽시계 기준 두 곳을 재시도 횟수나 주입한 시계로 바꿉니다.

---

## 나빠 보이지만 괜찮은 것

기준이 없으므로 모두 이번에 처음 적는 항목입니다. 근거가 저장소에 적혀 있는 것만 넣었습니다.

- **배포용 Editor에 갤러리와 프로브 코드 7,500줄이 들어 있습니다**(`Editor/Editor.pro:288-294`). 죽은 코드처럼 보이지만 CI 게이트가 배포할 실행 파일을 직접 몹니다. 위젯 수준 동작을 이 방식으로 확인한다는 것이 `docs/TestSeams.md:55-57`에 적혀 있습니다.
- **`catch (...)`가 COM과 DLL 진입점에 있습니다**(`platform/windows/ComBoundary.h:17-30`, `EqualizerAPO/ClassFactory.cpp:86`, `EqualizerAPO/DllMain.cpp:84`). 원인을 가리는 것처럼 보이지만 예외가 COM 경계를 넘지 않게 `HRESULT`로 바꾸는 자리이고, `RuntimeUtilityTests.cpp`의 COM boundary 사례가 그 변환을 고정합니다.
- **`PluginLoadQueue`가 자기 큐를 지우지 않습니다**(`Editor/helpers/PluginLoadQueue.cpp:230-232`). 누수처럼 보이지만 종료 뒤의 늦은 호출이 살아 있는 객체를 찾게 하려는 것이라고 그 자리에 적혀 있습니다.
- **`vstChunksEquivalent`가 ZIP 시간 필드를 지우고 비교합니다**(`Editor/helpers/VstChunkState.cpp:53-93`). 진짜 변경을 놓칠 수 있어 보이지만 받아들인 위험과 이유(#459)가 `Editor/helpers/VstChunkState.h:11-17`에 있고 `VstChunkStateTests.cpp`가 고정합니다.
- **`--selftest-crash`가 null에 씁니다**(`Editor/main.cpp:491-500`). 현장에서 크래시 핸들러가 dump를 남기는지 확인하는 자가 진단이라고 주석과 cppcheck 억제 사유에 적혀 있습니다.
- **설치기가 같은 릴리스의 `SHA256SUMS.txt`로 검증합니다**(`Installer/AutoInstaller.cpp:420-470`). 진위를 확인하지 못하는 검증으로 보이지만, 무결성만 본다는 것과 서명 전까지의 한계라는 것이 `:461-462`에 적혀 있습니다.
- **`icacls` 명령줄을 문자열로 만듭니다**(`services/security/AudioEngineAccess.cpp:170-187`). 입력은 설치 경로와 상수 SID뿐이고 실행 파일은 시스템 폴더에서 구해 응용 프로그램 이름으로 넘긴다는 경계가 `:18-21`에 적혀 있습니다.
- **승격된 훅이 `--caller-*` 인자를 읽습니다**(`Editor/import/LegacyMigration.cpp:398-433`). 승격되지 않은 쪽이 준 값을 믿는 것처럼 보이지만 힌트로만 쓰고 HKLM을 다시 읽으며, `Tests/EditorLogicTests/LegacyMigrationHookTests.cpp:243-288`이 안전하지 않은 호출자 경로를 거절하는지 확인합니다.
- **필터 하나의 `initialize()`가 던지면 설정 전체가 되돌아갑니다**(`engine/FilterEngine.Configuration.cpp:137-154`). 한 줄 때문에 전부 잃는 것처럼 보이지만, 실패한 load는 돌고 있던 설정을 유지한다는 계약이 `engine/FilterEngine.h:100-104`에 있고 `ConfigLoadTests`가 고정합니다.
- **`DeviceFilterFactory::endOfFile`이 stack 없이 `deviceMatches = true`로 되돌립니다**(`filters/DeviceFilterFactory.cpp:69-75`). 포함한 파일의 상태를 잃는 것처럼 보이지만, 바깥 파일에서 장치가 맞지 않았다면 Include 줄 자체가 실행되지 않았을 것이라는 이유가 그 자리에 있습니다.
- **`engine/IFilter.h`가 C++17로도 컴파일되게 묶여 있습니다**(`engine/IFilter.h:22-25`). C++20 저장소에서 이상해 보이지만 성능 계약 테스트 한 파일이 C++17로 고정돼 있고 그 이유가 `Tests/EngineOrchestrationTests/EngineOrchestrationTests.vcxproj:107-114`에 있습니다.
- **성능 테스트에 절대 기준이 있습니다**(`Tests/EngineOrchestrationTests/SampleIoTests.cpp:395-476`). 느린 러너에서 흔들릴 것 같지만 같은 바이너리 안의 기준 루프와의 비율이고 세 번 시도하며 ASan에서는 빠진다는 설명이 `:361-393`에 있습니다.
- **엔진을 쓰는 프로젝트의 Debug 구성이 받은 의존성으로는 링크되지 않습니다.** 고장처럼 보이지만 `EngineConsumer.props:22-25`와 `setup-build.ps1:176-180`에 적힌 결정입니다.
- **격주 감사가 Claude Code와 감사 스킬을 버전 고정 없이 설치합니다**(`.github/workflows/biweekly-audit.yml:17-29`). 메인테이너가 쓰는 것과 같은 것을 돌리려는 결정이라고 그 자리에 적혀 있습니다.
- **`SendRingWin32::touchAllPages`가 값을 읽어 그대로 다시 씁니다**(`runtime/ipc/SendRingWin32.cpp:44-58`). 의미 없는 대입 같지만 ring의 데이터를 건드리지 않고 페이지를 미리 올리는 방법이라고 주석과 cppcheck 억제 사유에 적혀 있습니다.

---

## 메인테이너에게 묻는 것

1. **TD-16의 배치는 받아들인 것입니까.** 사용자 프로필 안의 파일을 HKLM `Run`, audiodg의 COM 서버, 모든 사용자용 바로 가기가 가리킵니다. Velopack의 사용자별 설치를 택한 결과로 보이는데, `services/install/ApoRegistration.cpp:260-263`의 가정 한 줄 말고는 이 선택을 받아들인다는 기록을 찾지 못했습니다. 받아들인 것이라면 `SECURITY.md`에 적어 두면 다음 감사가 다시 묻지 않습니다.
2. **TD-15의 승격 상속은 의도입니까.** Voicemeeter 클라이언트를 승격된 채 띄우는 것은 원본 Equalizer APO에서 온 동작으로 보입니다. 상주 호스트는 #453에서 더해졌습니다. 둘을 승격 없이 띄워도 되는지 판단이 필요합니다.
3. **`Copy:`의 없는 채널을 어떻게 다뤄야 합니까(TD-03).** 지금 동작이 원본에서 온 것이라면 여기에 기대는 설정이 있을 수 있습니다. 항을 버릴지, 줄 전체를 거절할지 정해 주시면 됩니다.
4. **`vcredist143`로 충분합니까.** `.github/scripts/New-VelopackRelease.ps1:72`, `:178`은 `vcredist143`을 요구하는데 빌드는 `v145`입니다. Velopack이 이 ID로 확인하는 최소 버전을 이번에 확인하지 못했습니다. 사실이면 오래된 재배포 패키지가 깔린 기기에서 APO DLL이 올라가지 않을 수 있습니다.
5. **`icacls /T`가 junction을 따라갑니까(TD-17).** 확인하지 못했습니다. 따라간다면 TD-17은 High입니다.
6. **살아 있는 엔진의 재초기화가 실제로 얼마나 일어납니까(TD-09).** `docs/architecture/asio-host-study.md:137`은 지원되는 계약이라고 적었습니다. audiodg가 같은 APO 객체에 `LockForProcess`를 다시 부르는 빈도를 알면 심각도를 다시 매길 수 있습니다.
7. **실제 창을 쓰는 프로브 셋이 테스트 모드 목록에 없는 것은 의도입니까(TD-79).**
8. **`SendFilterFactory`가 load 확정 전에 공유 ring에 내보내는 것은 의도입니까(TD-45).** load가 되돌아가면 받는 쪽은 적용되지 않은 설정의 값을 이미 받은 상태입니다.
9. **`Agent.md`와 `CLAUDE.md` 중 어느 쪽이 기준입니까.** `CLAUDE.md`는 `Agent.md`에서 가져왔다고 적었지만 `Agent.md`에는 cppcheck, memcheck, panel-input-gate, PR 머지 규칙이 없고 툴셋 설명이 다릅니다.
10. **ASIO ring의 64채널 상한은 문서에 적어야 합니까.** `runtime/ipc/StreamRing.h:61-64`는 ASIO 하드웨어가 이 한도에 한참 못 미친다고 적었는데 MADI나 Dante 드라이버는 넘을 수 있고, `docs/features/asio.md`에는 한도가 없습니다. 넘으면 `createBuffers`가 메시지와 함께 실패합니다.
11. **`tools/Repair-EqualizerAPO.ps1`은 설치 파일에 들어갑니까.** `services/diagnostics/InstallDiagnostics.cpp:313`이 이 스크립트를 쓰라고 안내하는데, 포장 스크립트에서 이 파일을 싣는 줄을 찾지 못했습니다.
12. **`ARCHITECTURE_AUDIT.md`의 'Fine as is'와 다른 판단은 없습니다.** 다만 TD-48(코덱 모양)과 TD-49(스킨의 작은 파생값)는 그 문서가 구조 변경이 아니라고 한 것을 일관성 정리로 적은 것입니다.
