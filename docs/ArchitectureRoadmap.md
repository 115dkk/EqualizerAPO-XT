# 아키텍처 개편 로드맵

2026-07-25 저장소 전체 아키텍처 검토에서 나온 후보와 처리 상태를 기록합니다.
모듈 하나를 들여다봐서는 보이지 않고 두 모듈을 나란히 놓아야 드러나는 어긋남만 후보로 올렸습니다.

이 문서의 목적은 두 가지입니다. 남은 작업을 나중에 다른 사람이(또는 몇 달 뒤의 자신이)
맥락 없이 집어들 수 있게 하는 것, 그리고 **이미 검토해서 건드리지 않기로 한 것**을 기록해
다음 감사가 같은 제안을 되풀이하지 않게 하는 것입니다.

직전 배경으로, PR #222가 감사 99항목(A01~A29 포함)을 처리한 상태에서 시작했습니다.
그 29건이 다룬 영역은 후보에서 제외했습니다.

## 처리 완료

| 항목 | 내용 | PR | 릴리스 |
|---|---|---|---|
| S9 | 테스트 하네스 기본 정책을 Collect 로 전환, `report()` 미도달 백스톱 추가 | #223 | v2.26.3 |
| S10 | 쓰이지 않는 `aeffectx.h` 직접 include 제거 | #223 | v2.26.3 |
| S2 | `Common.vcxproj` 와 `Editor.pro` 소스 목록 어긋남을 잡는 CI lint | #223 | v2.26.3 |
| (파생) | `MultiConvolution` 소스 2개가 `Editor.pro` 에 누락돼 있던 것 | #223 | v2.26.3 |
| S1 | 명령어 분류를 `FilterFactoryRegistry::canonicalCommand` 하나로 통합 | #224 | v2.26.4 |
| S8 | 골든 오디오 회귀를 블록 경계 넘게 구동 | #225 | (없음) |
| S6 | RT 경로에서 뮤텍스·파일 입출력·힙 할당 제거 | #226 | v2.26.5 |

S1 이 고친 사용자 체감 결함은 셋입니다. 소수점 쉼표로 적은 `Preamp: -6,5 dB` 가 카드를 건드리는
순간 `-6.0` 으로 덮어써지던 것, REW·Dirac 이 기본으로 내보내는 `Filter 1: ON IIR ...` 에
카드 본문이 뜨지 않던 것, 그리고 엔진이 실행하지 않는 소문자 명령 줄에 Editor 가 살아 있는 카드를
그려 메모를 진짜 명령으로 바꿔놓던 것입니다.

## 남은 작업

### S5b. 설치를 트랜잭션으로 (착수 전)

`DeviceAPOInfo::install()` 은 약 40건의 레지스트리 변경을 일직선으로 수행하고 보상 동작이 없습니다.
원본 APO 를 `Child APOs` 에 먼저 복사한 뒤 FxProperties GUID 를 마지막에 쓰므로,
SFX 와 MFX 쓰기 사이에서 예외가 나면 절반만 연결된 장치가 남습니다.
그 뒤 `load()` 는 그것을 `installed = true` 로 보고합니다.
`reinstall()` 은 `uninstall(); load(); install();` 인데 가운데 `load()` 가 던지면 장치가 제거된 채로 남습니다.

올바른 모양이 같은 저장소 안에 이미 있습니다. `EqualizerAPO/DllMain.cpp` 의 `DllRegisterServer` 는
모든 실패 경로에서 두 GUID 를 되돌립니다.

**방향.** 적용 전에 의도한 변경 목록을 만들고, 적용하면서 이전 값을 기록하고, 실패 시 역순으로 복원합니다.
`reinstall()` 은 파괴 후 재구축이 아니라 계획 후 교체가 됩니다.
`DeviceAPOInfo.h` 에 실패한 `install()` 이 남기는 상태의 사후 조건을 명시해야 합니다. 지금은 정의되어 있지 않습니다.

**선행 조건.** S5a 의 레지스트리 포트와 인메모리 가짜가 있어야 이 변경을 테스트로 판정할 수 있습니다.
사람 손으로 확인할 수 없는 종류의 변경이므로 테스트 없이 착수하면 안 됩니다.

### S5c. 설치 경로 진단 (착수 전)

`devices/`, `RegistryHelper`, `ServiceHelper`, `DeviceSelector.cpp` 를 합쳐 약 950줄에 로그 호출이 0개입니다.
사용자가 설치 실패를 신고하면 `Editor.log` 에는 아무것도 없습니다. 설치를 실제로 수행한 DeviceSelector 가
어디에도 쓰지 않기 때문입니다. 그래서 진단 지식이 `tools/Diagnose-EqualizerAPO.ps1` 에 PowerShell 로,
GUID 를 다시 적어가며 프로그램 바깥에 존재합니다.

**방향.** 장치별 적용 결과를 값으로 돌려주게 합니다. 무엇을 발견했고(FxProperties 유무, 원본 저장 여부,
선택된 모드), 무엇을 썼고, 무엇이 어떤 Win32 상태로 실패했는지입니다.
DeviceSelector 가 그것을 화면에 그리고 `DeviceSelector.log` 에 남기며, Velopack 훅도 같은 모양을 씁니다.
그다음 `DeviceSelector --diagnose` 로 설치 경로·ACL·COM 등록·엔드포인트별 APO 사슬을 출력해,
PowerShell 스크립트의 검사를 프로그램 안으로 들여옵니다.

관련해서 `Editor/main.cpp` 는 Velopack 훅을 `LogHelper::useUserFile` 보다 **먼저** 실행합니다.
그래서 훅 출력이 `%TEMP%\EqualizerAPO.log` 로 떨어지고, 승격 후에는 그 `%TEMP%` 가 다른 계정의 것일 수 있습니다.
이것도 같이 다뤄야 합니다.

### S5d. audiodg 접근 권한을 한 모듈로 (착수 전)

"audiodg(LOCAL SERVICE)가 이 파일을 읽을 수 있는가"는 현장에서 가장 자주 나는 실패인데,
검사와 수리가 세 언어 네 곳에 흩어져 있습니다. `RegistryHelper::getFileAccessForUser`(AuthZ, 읽기 전용 6곳),
`ApoRegistration` 의 `icacls` 호출(부여), `Diagnose-EqualizerAPO.ps1`(`Get-Acl`), `Repair-EqualizerAPO.ps1`(`icacls` 재차).
앱은 조건을 **감지**할 수 있고 권한을 **부여**할 수도 있는데, 둘을 함께 하는 경로가 없어서
사용자에게 PowerShell 스크립트를 받으라고 안내합니다.

**방향.** `isReadableByAudioEngine(path)`, `grantEngineAccess(installRoot)`, `grantConfigAccess(configDir)` 를
가진 모듈 하나에 SID 와 권한 표를 한 번만 선언합니다. `getFileAccessForUser` 와 `isWindowsVersionAtLeast` 는
레지스트리 연산이 아니므로 `RegistryHelper` 에서 내보냅니다.
`ApoRegistration::install/uninstall` 이 승격을 가정만 하고 검사하지 않으므로 `requiresElevation()` 단언도 함께 둡니다.

### S7. 설정 파싱의 오류 모델 (착수 전)

`docs/ErrorHandlingPolicy.md` 는 네 가지 방식을 정해뒀는데(레지스트리·서비스는 예외, 설치는 `Result` 열거형,
VST 는 `bool`, 할당은 `nullptr`), **설정 파싱은 그 목록에 없습니다.** 믿을 수 없는 사용자 텍스트를 다루는
유일한 자리인데도 그렇습니다.

등록된 팩토리 15개 중 파싱 실패를 구조적으로 알리는 것은 2개뿐입니다. 나머지 13개는 사용자가 열어보지 않는
로그 파일에만 남깁니다. 엔진의 유일한 일반 진단은 추측입니다. '아는 명령인데 필터가 안 나왔고 예외 목록에도
없으면 매개변수가 잘못된 것'이라는 사후 판정이고, 그 추측이 틀리는 경우가 있어서 손으로 관리하는 예외 목록이
따로 존재합니다. Editor 쪽에는 오류 표시가 아예 없어서, 잘못된 `Convolution:` 줄과 그냥 적어둔 메모가
화면에서 구분되지 않습니다.

**방향.** 팩토리가 '이건 내 명령이었고 매개변수가 이래서 잘못됐다'고 말할 통로를 줍니다.
`ConfigLoadTrace` 싱크가 이미 그 운반체이고 Editor 가 이미 수집하므로 종류 하나만 더하면 됩니다.
그 뒤 추측 블록과 `commandsWithoutFilter` 예외 목록을 지웁니다(약 35줄이 사라지고 호출자 쪽에 아무것도
되살아나지 않습니다). 정책 문서에 설정 파싱 줄을 추가합니다. 보고하고 계속하되 로드를 중단하지 않으며 줄 단위로 구조화합니다.

### S3. 스킨의 공용 좌표 어휘와 알파 토큰 (착수 전)

스킨 층은 C++ 11,947줄에 QSS 12,177줄입니다. 차별화 자체는 스킨 헌법(`docs/skins/README.md`)이 지키는
설계이고 유지해야 합니다. 문제는 **복제되는 것이 디자인이 아니라 산술**이라는 데 있습니다.

- 스코프 레인 좌표를 4개 스킨이 글자 그대로 같은 식으로 다시 계산합니다. 원본은 `FilterCardRow` 가 이미 갖고 있습니다.
- 그래프 눈금 라벨 사각형이 X축 9곳, Y축 7곳에 흩어져 있고 Y축 안쪽 여백이 스킨마다 4, 5, 6, 8 입니다. 아무도 정한 적 없는 차이입니다.
- `prepareCommandRow` 만 21개 훅 중 유일하게 `SkinTokens&` 를 못 받아, 5개 스킨이 전부 `SkinManager::instance()` 를 직접 부릅니다.
- `@TOKEN@` 치환에 알파 형태가 없어 `rgba(@ACCENT@, 0.30)` 을 쓸 수 없고, 팔레트 값을 손으로 펼쳐 적습니다. Studio 한 장에만 `rgba()` 리터럴이 285개입니다.

**방향.** `SkinPaint.h` 가 이미 '디자인 결정을 담지 않는 것'을 맡는 자리로 선언돼 있으니 거기에 좌표 어휘를 더합니다.
행 위젯이 확정한 레인 좌표를 `CommandRowInfo` 에 실어 보내면 스킨은 '레인이 어떻게 생겼나'만 답하고
'어디인가'는 묻지 않게 됩니다.

**검증 수단이 확실합니다.** 갤러리의 PNG SHA-256 비교로 그림이 안 바뀌었음을 증명할 수 있습니다.
스킨 하나씩 나눠 진행하면 됩니다.

### S4. 스킨 명단 단일화 (착수 전)

스킨을 하나 추가하려면 18곳을 고쳐야 하고, 빠뜨리면 **조용히 Studio 로 보입니다**.
`resolveId` 가 못 찾으면 `"studio"` 로 떨어지기 때문입니다. 컴파일되고 등록되고 메뉴에도 뜨는 새 스킨이
Studio 모습으로 그려지는데 오류는 어디에도 나지 않습니다.

`Skins::all()` 이 명단을 아는 유일한 자리인데 명단이 필요한 쪽은 아무도 그것을 보지 않습니다.
`resolveId`, `tokens()`, `DeviceSkinPainter::forSkin`, `DeviceSelector/main.cpp`, `InfrastructureTests` 두 곳이
각자 다시 적습니다.

**방향.** `Skins::all()` 을 유일한 명단으로 삼고 나머지를 거기서 끌어냅니다.
DeviceSelector 는 설계상 ISkin 클래스를 링크할 수 없으니 명단의 데이터 절반은 이미 토큰 표가 있는
`SkinThemeData` 에 둡니다. 그리고 '스킨 추가하기' 절차를 `docs/skin-hooks.md` 에 씁니다.
그 문서 45행이 지금 틀린 내용을 가리키고 있으니(스킨이 `Skins.cpp` 에서 오버라이드를 구현한다고 적혀 있는데,
스킨이 각자 TU 로 갈라진 뒤로는 사실이 아닙니다) 함께 고칩니다.

### 루트 구조 정리 (착수 전)

루트에 엔진 소스 19개가 남아 있는데 `engine/` 과 `devices/` 는 이미 만들어져 있습니다.
`FilterEngine.h` 와 `FilterEngine.cpp` 는 루트에, `FilterEngine.Process.cpp` 같은 본문은 `engine/` 에 있어서
**폴더 이름이 가리키는 클래스의 선언이 그 폴더에 없습니다.**

옮길 것은 `engine/` 으로 `FilterEngine`, `FilterConfiguration`, `ConfigurationFileReader`, `ConfigLoadTrace`,
`IFilter`, `IFilterFactory`, `devices/` 로 `DeviceAPOInfo`, `AbstractAPOInfo`, `VoicemeeterAPOInfo` 입니다.
`stdafx` 는 `Common.vcxproj` 의 PCH 이고 105개 파일이 포함하므로 루트에 둡니다.
`version.h` 는 CI 의 `Bump-Version.ps1` 이 직접 쓰는 릴리스 계약이라 옮기면 위험만 늡니다.

비용은 include 약 117개 치환이고 전부 컴파일러가 잡아줍니다. 조용히 실패할 수 있는 종류가 아닙니다.
다만 `Common.vcxproj`, `Editor.pro`, 테스트 `.vcxproj`, 나머지 `.pro` 의 파일 목록도 함께 고쳐야 합니다.

가치가 낮고 churn 이 크므로 다른 작업이 없는 조용한 시점에 한 번에 하는 편이 좋습니다.

### 작은 것들

- `FilterConfiguration.cpp` 의 `typeid(*filter).name()` 은 타입마다 첫 호출에서 CRT 안에서 할당합니다.
  오디오 스레드에 남은 마지막 첫-호출 할당이고 프로파일링이 켜졌을 때만 발생합니다.
  `FilterInfo` 에 라벨 포인터를 초기화 시점에 채워두면 됩니다.
- `Tests/EditorLogicTests/EditorLogicTests.vcxproj` 는 22개짜리 자체 소스 목록을 갖고 있고 아무것도 검사하지 않습니다.
  `Test-SourceSync.ps1` 과 같은 종류의 문제입니다.
- `FilterCardRow` 의 `info.command.toLower()` 와 `FilterCardModel::commandIconResource` 는 아직 소문자로 다룹니다.
  둘 다 스킨 QSS 선택자 키와 픽토그램 조회용이라 명령 판정이 아니지만, 저장소 안에 명령 문자열을 소문자로
  다루는 자리가 남아 있다는 사실은 기록해 둡니다. 바꾸려면 스킨 5개와 스타일시트 4개를 같이 고쳐야 합니다.
- `helpers/VSTPluginLibrary.h` 가 공개 헤더에서 `aeffectx.h`(98KB)를 노출합니다.
  Editor 의 6개 TU 가 그것을 통과시키면서 실제로는 멤버 4개만 씁니다.
  얇은 정면 인터페이스를 두고 원래 헤더를 호스트 TU 전용으로 감추면 컴파일 시간이 실제로 줄어듭니다.
- `HybridConvTests.vcxproj` 는 Release 세 구성에만 `/WHOLEARCHIVE` 가 있고 Debug 에는 없습니다.
  `FilterFactoryRegistryTests` 가 어휘 비어 있음을 `require` 로 막으므로 Debug 실행이 그 지점에서 멈춥니다.
  CI 는 Release 만 빌드하므로 파이프라인에는 영향이 없지만 Debug 로 디버깅하는 사람에게는 걸림돌입니다.

## 건드리지 않기로 한 것

검토해서 후보에서 제외한 것들입니다. 근거를 남겨 다음 감사가 같은 제안을 되풀이하지 않게 합니다.

- **`ConfigSwapChannel` 과 `processImpl`.** 세 개의 포인터 슬롯, 세마포어 퍼밋, 지연 해제는 전부 특정 실시간
  보장을 삽니다. 은퇴한 설정의 파괴를 생산자의 다음 `publish()` 로 미루는 것, C++ 뮤텍스 없이
  `exchange` 와 `ReleaseSemaphore` 만 쓰는 것, 등출력 크로스페이드 표를 미리 계산해 프레임마다 `cos` 를
  부르지 않는 것 모두 의도입니다.
- **`IFilter` 가 얇은 것.** 공통 기반 클래스를 만들면 회수량이 약 70줄인데(필터 로직 총량 약 2,300줄) 블록당
  10~15회 불리는 함수에 가상 계층이 하나 더 생깁니다. 부족한 것은 클래스가 아니라 헤더에 적히지 않은 계약입니다.
- **건드리지 않은 줄의 원문 보존.** `FilterListModel` 이 편집되지 않은 줄을 그대로 돌려주는 덕분에 손으로 쓴
  1.4.2 설정이 열고 저장해도 주석·모르는 명령·순서·간격·대소문자를 유지합니다. 정규 포매터를 두자는 제안은
  이것을 파괴하므로 하면 안 됩니다.
- **`filters/*Command.{h,cpp}` 공용 코덱.** Qt 를 모르는 `parse`/`serialize` 쌍을 엔진 팩토리와 구형 GUI 와
  카드 편집기가 함께 씁니다. 아홉 번 성공한 패턴이고, S1 의 Preamp 는 이 패턴을 안 따른 예외였을 뿐입니다.
- **`FilterConfiguration` 의 매크로 디인터리브.** 1/2/6/8 채널 분기 매크로는 흔한 레이아웃에서 채널 수를
  컴파일 시간 상수로 만들려고 있습니다. 주석에 측정값이 있고 ARM64 에서 Highway 인터리브 저장이 스칼라보다
  느려서 뺐다는 기록까지 있습니다. 일반 루프로 바꾸면 쓰기 경로에서 측정된 2.8~5.1배를 도로 내놓습니다.
- **Editor 가 `Common.lib` 를 링크하지 않는 것.** 기록된 메인테이너 결정입니다(`Editor.pro:623-630`, 감사 #146 TD013).
  분석 패널의 `FilterEngine` 이 그 변형의 `/arch` 로 돌아야 하기 때문입니다.
  S2 의 lint 는 이 결정을 뒤집는 것이 아니라 그 결정이 만든 손 동기화 비용만 없앱니다.
- **설치 중 소유권 획득 재시도와 두 번째 `Initialize`.** 드라이버가 소유한 FxProperties 키는 소유권을
  가져오기 전에는 승격된 SYSTEM 에도 쓰기를 거부하고, 미리 검사할 방법은 다른 오디오 프로세스와의 경합이 됩니다.
  `AUTOCONVERTPCM` 재시도도 이슈 #75 의 현장 근거가 주석에 남아 있습니다.
- **골든 회귀 기준값 재생성.** 블록 처리로 전환할 때 재생성하지 **않은** 것이 의도입니다.
  통짜 버퍼로 만든 기준값을 블록 처리가 부동소수점 잡음 수준에서 재현하므로, 같은 파일이 출력값뿐 아니라
  '신호를 어떻게 잘라 넣든 결과가 같다'는 사실까지 못박습니다. 자세한 내용은
  `Tests/AudioRegressionTests/references/README.md` 에 있습니다.

## 작업 순서에 관한 기록

이번 라운드에서 두 번, 보고서가 제안한 순서를 바꿨고 두 번 다 그 편이 나았습니다.

**회귀를 먼저 넓히고 실시간 코드를 고쳤습니다.** 보고서는 S6 과 S8 을 한 묶음으로 제안했는데 S8 을 먼저 했습니다.
그래서 뮤텍스를 걷어낸 뒤 '출력이 안 바뀌었다'를 아홉 사례가 판정해 줄 수 있었습니다.
순서를 반대로 했으면 근거가 통짜 버퍼 비교뿐이었을 텐데, 그건 블록 간 상태 처리가 달라져도 잡지 못합니다.

**테스트를 먼저 깔고 설치 동작을 고칩니다.** S5 를 셋으로 쪼갠 것도 같은 이유입니다.
설치 코드가 잘못되면 사용자 오디오 장치가 걸리는데 그건 자동으로 확인할 방법이 마땅치 않습니다.
레지스트리 포트와 인메모리 가짜를 먼저 넣어 테스트가 지켜보는 상태를 만든 다음 트랜잭션 구조로 갑니다.

원칙은 하나입니다. **판정할 수단을 먼저 만들고 그다음에 판정 대상을 바꿉니다.**
