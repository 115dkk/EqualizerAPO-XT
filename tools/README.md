# tools/

EqualizerAPO-XT 설치 진단과 복구를 위한 PowerShell 헬퍼.

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

이 스크립트는 install hook fix(v0.3 이후)가 들어가기 전에 설치한 사용자가 재설치 없이 문제를 해결할 수 있도록 두는 것입니다. v0.3 이후로 새로 설치한 사용자는 install hook이 동일한 ACL을 자동으로 적용하므로 이 스크립트가 필요하지 않습니다.

## 배경

`audiodg.exe`는 LOCAL SERVICE 계정으로 실행되어 APO COM 객체를 로드합니다. Velopack은 EqualizerAPO를 `%LocalAppData%\EqualizerAPO-XT-*\current\` 아래에 설치하는데, 이 사용자 프로필 하위 트리의 기본 ACL은 설치한 사용자와 Administrators만 접근을 허용합니다. LOCAL SERVICE에는 권한이 없으므로 audiodg가 EqualizerAPO.dll을 읽지 못하고, audio engine이 device를 invalidated 상태로 만들어 후속 `IAudioClient::GetMixFormat`과 `Initialize` 호출이 `E_ACCESSDENIED`(액세스가 거부되었습니다)를 반환합니다. 한 device 안에서 install mode 세 가지(SFX/EFX, SFX/MFX, LFX/GFX)를 차례로 시도하므로 오류 다이얼로그가 여러 개 쌓이는 것이 보입니다.

`helpers/ApoRegistration.cpp`의 `install()`이 v0.3부터 install root에 LOCAL SERVICE RX, config dir에 LOCAL SERVICE M을 grant하도록 보강되어, 새 설치에서는 이 문제가 발생하지 않습니다.
