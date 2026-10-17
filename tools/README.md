# tools/

EqualizerAPO-XT 설치 진단과 복구를 위한 PowerShell 헬퍼.

## 먼저: 프로그램에 들어간 진단

같은 검사가 이제 프로그램 안에 있습니다. 스크립트를 받을 필요가 없습니다.

```powershell
Editor.exe --diagnose
```

설치 경로, COM 등록, audiodg(LOCAL SERVICE)와 Users의 접근 권한, 엔드포인트별 APO 사슬을 콘솔에 출력하고 `%LOCALAPPDATA%\EqualizerAPO\logs\diagnose-<시각>.txt`에도 남깁니다. 읽기 전용이고 권한 상승이 필요 없습니다.

`DeviceSelector.exe --diagnose`도 같은 보고서를 냅니다. 다만 DeviceSelector는 `requireAdministrator`로 링크되므로 읽기만 하더라도 UAC 프롬프트가 뜹니다. 그래서 사용자에게 안내할 것은 Editor 쪽입니다.

아래 두 스크립트는 남겨둡니다. 설치가 아예 없거나 실행 파일이 실행되지 않는 기계에서도 돌아가고, 복구 쪽은 아직 프로그램 안에 대응물이 없습니다.

## Diagnose-EqualizerAPO.ps1

읽기 전용 진단 스크립트입니다. 설치 경로, ACL, COM 등록 상태, MMDevices의 APO 체인을 덤프하고 Device Selector에서 "GetMixFormat / Initialize 액세스 거부"가 발생하는 조건이 있는지 점검합니다. 권한 상승 없이 실행할 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Diagnose-EqualizerAPO.ps1
# 결과를 파일로 저장하려면:
.\Diagnose-EqualizerAPO.ps1 > eapo-report.txt
```

스크립트가 "MISSING: LOCAL SERVICE grant on install root"를 경고하면 audiodg.exe가 APO DLL을 읽지 못해 audio engine이 device를 invalidated 상태로 만든 경우입니다. 이 경우 `Repair-EqualizerAPO.ps1`을 elevated PowerShell에서 실행하면 됩니다.

## Repair-EqualizerAPO.ps1

기존 설치본을 재설치 없이 복구합니다. install hook이 적용하는 것과 동일한 ACL을 install root와 config dir에 다시 적용하고, Audiosrv를 재시작해 audiodg가 새 ACL로 APO를 다시 로드하게 합니다. **elevated PowerShell**이 필요합니다.

```powershell
# 관리자 권한 PowerShell에서
powershell -ExecutionPolicy Bypass -File .\Repair-EqualizerAPO.ps1
```

`-SkipServiceRestart`를 주면 ACL만 손보고 Audiosrv는 건드리지 않습니다. 이 경우 수동으로 재시작하거나 재부팅해야 audiodg가 변경된 ACL을 본다.

설치 시기와 관계없이 진단에서 LOCAL SERVICE의 접근 권한이 없다고 나오면 이 스크립트로 복구합니다. 복구 후 `Editor.exe --diagnose`로 다시 확인하십시오.

## 배경

`audiodg.exe`는 LOCAL SERVICE 계정으로 실행되어 APO COM 객체를 로드합니다. Velopack은 EqualizerAPO를 `%LocalAppData%\EqualizerAPO-XT-*\current\` 아래에 설치하는데, 이 사용자 프로필 하위 트리의 기본 ACL은 설치한 사용자와 Administrators만 접근을 허용합니다. LOCAL SERVICE에는 권한이 없으므로 audiodg가 EqualizerAPO.dll을 읽지 못하고, audio engine이 device를 invalidated 상태로 만들어 후속 `IAudioClient::GetMixFormat`과 `Initialize` 호출이 `E_ACCESSDENIED`(액세스가 거부되었습니다)를 반환합니다. 한 device 안에서 install mode 세 가지(SFX/EFX, SFX/MFX, LFX/GFX)를 차례로 시도하므로 오류 다이얼로그가 여러 개 쌓이는 것이 보입니다.

`services/install/ApoRegistration.cpp`의 `install()`은 설치 폴더에 LOCAL SERVICE의 읽기·실행 권한을, 설정 폴더에는 수정 권한을 부여합니다. 이 권한이 없으면 복구 스크립트로 다시 부여할 수 있습니다.
