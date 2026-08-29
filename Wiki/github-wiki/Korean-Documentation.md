# 사용자 문서
**EqualizerAPO-XT**의 사용자 문서입니다. 직접 APO를 만들거나 소스에서 빌드하려면 [개발자 문서](Korean-Developer-documentation)를 보세요. 설치를 마친 뒤 명령을 자세히 알고 싶으면 [설정 레퍼런스](Korean-Configuration-reference)에 전부 정리해 두었습니다.
## 설치
XT는 호환성을 위한 작은 진입 설치 파일과 CPU별 64비트 패키지 여섯 개를 함께 배포합니다. APO/애플리케이션 본체에는 **32비트 빌드가 없으며** x64와 ARM64만 지원합니다.

1. [릴리스 페이지](https://github.com/115dkk/EqualizerAPO-XT/releases)에서 **`EqualizerAPO-XT-Setup.exe`**를 받습니다.
1. 인터넷에 연결한 상태에서 실행합니다. CPU의 실제 아키텍처와 CPU·Windows가 함께 지원하는 가장 높은 AVX 수준을 감지하고, 맞는 Velopack 설치 파일을 받은 뒤 릴리스의 `SHA256SUMS.txt`와 대조해 검증하고 나서 실행합니다.
1. 오프라인으로 옮기거나 복구하거나 특정 빌드를 의도적으로 설치할 때는 채널별 `…-Setup.exe`도 쓸 수 있습니다. 채널은 `x64-sse2`, `x64-avx`, `x64-avx2`, `x64-avx512`, `x64-avx10-1`, `arm64-neon`입니다. 대상 기기의 지원 여부를 확실히 알 때만 직접 고르세요. 정확한 선택·무결성 검사 방식은 [자동 감지 설치 파일 설계 문서](https://github.com/115dkk/EqualizerAPO-XT/blob/main/docs/AutoDetectInstaller.md)에 있습니다.
1. 선택된 Velopack 설치 프로그램이 애플리케이션을 풀고 EqualizerAPO-XT를 등록합니다. 설치 경로를 외울 필요는 없습니다. 위치는 레지스트리에 기록됩니다([아래](#설치-위치) 참고).
1. 처음 설치하면 **장치 선택기(Device Selector)**가 열립니다. Equalizer APO를 적용할 재생 장치나 녹음 장치를 선택합니다. 잘 모르겠으면 기본 출력 장치를 고르면 됩니다. 어느 것이 기본 장치인지는 **시작 → 설정 → 시스템 → 소리**에서 볼 수 있습니다. 나중에 장치를 추가하거나 빼고 싶으면 설치 폴더에서 장치 선택기를 다시 실행하면 됩니다.
1. Windows가 오디오 서비스를 다시 시작하도록 두거나 재부팅합니다. 새로 등록한 APO는 오디오 엔진이 재시작해야 적용됩니다.
1. 서비스가 다시 시작되면 APO가 활성화됩니다. 기본 예제 설정에서는 음량이 조금 줄고 저역이 약간 올라가는 정도라 변화가 크지 않습니다. 쓸모 있게 바꾸려면 [첫 설정](#첫-설정)으로 넘어가세요.

### 설치 위치
EqualizerAPO-XT는 경로를 레지스트리 키 **`HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO`**에 기록합니다.

* `InstallPath` — 애플리케이션이 설치된 디렉터리입니다.
* `ConfigPath` — 설정 파일이 들어 있는 `config` 폴더입니다. 일반적인 XT 설치는 `%LOCALAPPDATA%\EqualizerAPO-XT\config`를 쓰지만, 사용자가 의도적으로 고른 경로는 보존하므로 레지스트리 값이 기준입니다.

설정 폴더로 가는 가장 쉬운 방법은 **설정 편집기(Configuration Editor)**를 여는 것입니다. 편집기가 이 폴더를 바로 가리킵니다. 폴더에는 자동으로 불러오는 기본 파일 `config.txt`와 함께 바로 쓸 수 있는 예제들이 들어 있습니다. `example.txt`, `demo.txt`, `convolution.txt`, `iir_lowpass.txt`, `multichannel.txt`, `selective_delay.txt`입니다.

### 자동 업데이트
XT는 [Velopack](https://velopack.io/)으로 설치되므로, 처음부터 다시 설치하지 않고 빌드 채널별로 업데이트가 전달됩니다.

* **UpdateChecker**가 로그온할 때 예약 작업으로 실행되어, 해당 채널에 새 릴리스가 있으면 알려줍니다. GitHub 릴리스 피드에서 변형에 맞는 항목을 확인하고, 24시간 간격 제한과 사용자가 건너뛴 버전을 지킵니다.
* **설정 편집기**도 스스로 업데이트합니다. 실행하고 약 1분 뒤 해당 채널의 새 빌드를 백그라운드에서 조용히 내려받고, 편집기를 닫을 때 조용히 적용합니다. 새 버전은 다음에 실행할 때 올라옵니다.

## 첫 설정
1. 설정 폴더를 엽니다([위](#설치-위치) 참고). 기본 파일은 `config.txt`이고, Equalizer APO가 자동으로 불러오며 저장할 때마다 다시 읽습니다.
1. `config.txt`를 설정 편집기나 텍스트 편집기로 엽니다. 프리앰프 값을 정한 뒤 `example.txt`를 포함하도록 되어 있습니다. APO가 도는지 확인하려면 아무 음원이나 재생하면서 소리가 나는 동안 `Preamp` 값을 바꿔 보세요. 파일을 저장하는 순간 음량이 바뀌어야 합니다.
1. 직접 보정을 만들려면 전통적으로 [Room EQ Wizard](https://www.roomeqwizard.com/)(REW)로 시스템을 측정한 뒤 필터 텍스트로 내보냅니다. EqualizerAPO-XT에는 필터를 바로 추가하고 조정하는 그래픽 **설정 편집기**도 들어 있어, 소소한 조정에는 이쪽이 더 편하다는 사람이 많습니다.

<img src="RoomEQWizard.png" width="600"><br><em>Room EQ Wizard (A–F 표시는 아래 절차에서 언급합니다)</em>

REW 사용법을 전부 다루는 것은 이 문서의 범위를 벗어나지만, 측정에서 필터까지의 기본 흐름은 이렇습니다.

1. **Measure**(표시 **A**)를 눌러 측정 대화상자를 엽니다. 먼저 **Check Levels**로 출력 음량을 적당히 맞춘 뒤 **Start Measuring**을 실행합니다. 끝나면 주파수 응답 그래프가 나타납니다.
1. **EQ**(표시 **B**)를 누르고 이퀄라이저 종류(표시 **C**)를 고릅니다. **Generic**을 쓰거나, Q 대신 대역폭을 선호하면 **FBQ2496**을 씁니다. 다른 종류는 호환을 보장하지 않습니다.
1. **EQ Filters**(표시 **D**)를 누릅니다. *Control*을 *Manual*로, *Type*을 *PK/PEQ*로 놓고 *Frequency*, *Gain*, *Q*/*Bw Oct*를 조정해 필터를 추가합니다. 그래프가 실시간으로 갱신됩니다. 룸 보정에는 보통 피킹 필터가 알맞지만, 다른 [필터 종류](Korean-Configuration-reference#filter)도 쓸 수 있습니다.
1. **Save this filter set**(표시 **E**)으로 REW 필터 세트를 저장해 두면 나중에 다시 불러올 수 있습니다.
1. Equalizer APO가 읽는 형식으로 내보냅니다. 메인 창에서 **File**(표시 **F**) → **Export** → **Filter Settings as text**를 골라 설정 폴더에 새 이름으로 저장합니다.
1. `config.txt`의 `Include` 줄이 방금 만든 파일을 가리키도록 고칩니다. 변경은 즉시 적용됩니다.

이렇게 하면 첫 설정이 완성됩니다. 필터, 채널 라우팅, 딜레이, 컨볼루션, 그래픽 EQ, 표현식 언어 같은 전체 문법은 [설정 레퍼런스](Korean-Configuration-reference)에 있습니다. 측정 쪽을 더 깊이 알고 싶으면 REW [도움말](https://www.roomeqwizard.com/help/)을 보세요.

## 컨볼루션
XT 포크를 쓰는 이유 중 하나가 컨볼루션 지원입니다. [Convolution](Korean-Configuration-reference#convolution) 명령은 사운드 파일(WAV, FLAC, OGG 등 [libsndfile](https://libsndfile.github.io/libsndfile/)이 다루는 형식)에서 임펄스 응답을 불러와 선택한 채널과 합성곱합니다. XT는 원본의 임펄스 응답 길이 제한을 없애고 설정을 단순하게 만들었습니다. 함께 들어 있는 `convolution.txt`가 출발점입니다. 임펄스 응답의 샘플레이트는 오디오 장치의 샘플레이트와 같아야 하며, 설정 폴더 안의 임펄스 응답 파일이 바뀌면 설정이 자동으로 다시 로드됩니다.

## 문제 해결
Equalizer APO가 기대대로 동작하지 않을 때 흔한 원인들을 모았습니다.

### 원본 APO와 장치 선택기
Equalizer APO는 기본적으로 사운드 카드 드라이버에 딸려 온 효과(원본 APO)를 자기 옆에서 계속 동작하게 두려고 합니다. 일부 시스템에서는 이 연결이 잡음을 일으킵니다. 재생이나 녹음이 불안정하면 **장치 선택기**를 열어 해당 장치를 고르고, 문제 해결 옵션을 켠 뒤 **Use original APO** 체크박스 두 개를 모두 해제하세요.

<img src="UseOriginalAPO.png" width="500"><br><em>장치 선택기에서 원본 APO 끄기</em>

이렇게 하면 드라이버가 자기 APO로 제공하던 효과는 사라집니다. 그래서 한쪽만 해제해 일부 기능을 남겨 두는 방법도 있습니다. 어떤 드라이버는 다른 APO가 감지되면 자기 옵션을 막아 버리는데, **Install APO** 체크박스로 Equalizer APO를 pre-mix 단계나 post-mix 단계 중 한쪽에만 설치하면 나머지 단계에서 드라이버 기능 일부를 되살릴 수 있습니다.

### 제어판에서 오디오 향상이 꺼져 있음
설정 파일을 아무리 바꿔도 소리에 변화가 없다면, Windows 소리 설정에서 해당 장치의 APO가 꺼져 있을 수 있습니다. **시작 → 설정 → 시스템 → 소리**에서 장치 속성을 열고 다음을 확인합니다.

* **향상 기능**(Enhancements) 탭이 있으면, 목록에 있는 향상 기능을 하나도 안 쓰더라도 **모든 향상 기능 사용 안 함**이 해제되어 있는지 확인합니다.
* 향상 기능 탭이 없으면 **고급**(Advanced) 탭을 열어 **오디오 향상 기능 사용**이 켜져 있는지 확인합니다.

<img src="EnhancementsTab.png" width="350"><br><em>"향상 기능" 탭 — "모든 향상 기능 사용 안 함"을 해제한 상태로 둡니다</em>
<img src="NoEnhancementsTab.png" width="350"><br><em>"고급" 탭 — "오디오 향상 기능 사용"을 켠 상태로 둡니다</em>

### 로그 파일
Equalizer APO는 치명적인 문제를 만나면 다음 파일에 한 줄을 기록합니다.

```
C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log
```

정상일 때는 이 파일이 아예 없습니다. 오류가 날 때만 만들어집니다. 더 자세한 정보가 필요하면 추적 출력을 켤 수 있습니다. `regedit.exe`를 열고 `HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO`로 가서 **`EnableTrace`** 값을 `true`로 바꿉니다. 그러면 평소 재생이나 녹음 중에도 `(TRACE)` 표시가 붙은 줄이 기록되어, 설정 파일이 어떻게 해석되는지 확인할 때 유용합니다. 끝나면 로그가 불필요하게 커지지 않도록 `EnableTrace`를 다시 `false`로 되돌리세요.

XT의 사용자용 진단 자료는 `%LOCALAPPDATA%\EqualizerAPO\logs` 아래에 모입니다.

* `Editor.log` — Editor, 설치/업데이트 훅, 저장 실패를 기록합니다.
* `DeviceSelector.log` — 장치 설치, 제거, 복구를 기록합니다.
* `Editor.exe --diagnose`를 실행하면 `diagnose-<시간>.txt`를 이 폴더와 연결된 콘솔에 씁니다. 설치 상태만 검사하며 관리자 권한은 필요 없습니다.
* Editor 크래시 미니덤프와 텍스트 보고서는 `crash` 하위 폴더에 들어갑니다.

### 독점 모드, ASIO, 저지연 스트림, raw 스트림
Equalizer APO는 Windows 오디오 엔진 안에서 APO로 동작합니다. 그래서 엔진이 섞는 스트림은 모두 처리하고, 엔진을 비켜 가는 스트림은 하나도 처리하지 못합니다. 어느 쪽인지는 설치가 아니라 응용 프로그램이 정합니다.

* **WASAPI 독점 모드**(재생기의 'WASAPI 독점' 출력, 스트리밍 앱의 '독점 모드' 스위치, HQPlayer, JRiver 등)는 소리를 응용 프로그램에서 드라이버로 바로 보냅니다. 엔진이 보지 못하므로 APO는 실행되지 않고, 장치 선택기의 어떤 옵션도 이것을 바꾸지 못합니다. EQ를 남기는 방법은 둘입니다. 응용 프로그램의 출력을 공유 모드로 바꾸거나, 응용 프로그램에 ASIO 출력이 있으면 `<드라이버 이름> (EQ APO XT)` 항목을 고르는 것입니다([ASIO](https://github.com/115dkk/EqualizerAPO-XT/blob/main/docs/features/asio.md)).
* **ASIO**도 같은 방식으로 엔진을 비켜 갑니다. 그런 스트림에 EQ를 남기는 방법이 XT의 래퍼 항목입니다.
* **저지연 공유 스트림**(IAudioClient3나 AudioGraph로 작은 버퍼를 요청하는 게임, DAW, 통화 앱)은 엔진 안에 남아 다른 스트림과 똑같이 Equalizer APO를 지납니다. APO가 받는 블록이 작아질 뿐이며, 32프레임 블록까지 돌려 봤습니다. 알아 둘 것이 둘 있습니다. 컨볼루션 필터의 블록은 엔진 주기를 따르므로 긴 임펄스 응답은 그런 앱이 도는 동안 CPU를 더 씁니다. 그리고 그런 앱이 돌 때만 소리가 끊긴다면 `EnableTrace`를 켜고([로그 파일](#로그-파일)) 앱이 시작할 때 찍히는 `LockForProcess` 줄을 확인해 그 로그를 제보에 붙여 주세요.
* **raw 모드** 스트림(`AUDCLNT_STREAMOPTIONS_RAW`를 켠 응용 프로그램)은 스트림 슬롯(SFX)은 건너뛰지만 post-mix 슬롯은 지납니다. 모드 효과는 Windows 10부터, 엔드포인트 효과는 항상 실행됩니다. 두 단계를 모두 체크한 설치는 설정의 post-mix 부분을 그런 스트림에도 계속 적용합니다. raw 모드가 있는지는 드라이버가 정하며, 예를 들어 가상 케이블에는 없습니다.

### 하드웨어 가속 OpenAL
OpenAL을 쓰는 애플리케이션은 대개 APO를 지원하는 DirectSound로 폴백하므로 문제가 없습니다. 다만 일부 제조사는 하드웨어에 직접 접근해 APO를 우회하는 하드웨어 가속 OpenAL 라이브러리를 제공합니다. 하드웨어 가속 OpenAL에 APO 지원을 추가할 방법은 없으므로, 애플리케이션을 다른 출력 백엔드로 바꾸거나 OpenAL을 소프트웨어 모드로 돌리는 수밖에 없습니다. 예를 들어 `OpenAL32.dll`을 [OpenAL Soft](https://openal-soft.org/) 빌드로 교체하거나, `C:\Windows\System32` 또는 `C:\Windows\SysWOW64`에 있는 제조사 하드웨어 OpenAL 라이브러리(흔히 `*_oal.dll` 형태)의 이름을 바꾸는 방법이 있습니다. 뒤쪽 방법은 사운드 드라이버를 건드리는 것이라 공식적으로 지원되지 않습니다.

## 함께 보기
* [설정 레퍼런스](Korean-Configuration-reference) — 모든 명령과 문법.
* [개발자 문서](Korean-Developer-documentation) — XT 빌드와 직접 APO 만들기.
* [English version of this page](Documentation)
