<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ko_KR">
<context>
    <name>DeviceSelector</name>
    <message>
        <location filename="../DeviceSelector.cpp" line="195"/>
        <source>Multi-core convolution processing</source>
        <translation>멀티코어 컨볼루션 처리</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="197"/>
        <source>Spreads the work of Convolution and MultiConvolution over several CPU cores, so long impulse responses on many channels no longer load a single core. The sound and the latency stay the same.

It is optional because it starts extra audio threads. Turn it off if it causes dropouts on your system. Applies to every device.</source>
        <translation>Convolution과 MultiConvolution의 연산을 여러 CPU 코어에 나누어 처리하므로, 많은 채널에서 긴 임펄스 응답을 처리할 때 한 코어에 부하가 몰리지 않습니다. 소리와 지연은 그대로 유지합니다.

이 옵션을 켜면 오디오 스레드를 추가로 시작합니다. 사용 중인 PC에서 이 옵션 때문에 소리가 끊기면 끄십시오. 모든 장치에 적용됩니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="210"/>
        <source>Resample received audio</source>
        <translation>전송된 오디오 리샘플링</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="212"/>
        <source>Converts audio that Send: lines in other endpoints&apos; configurations hand to this endpoint when their sample rate differs from this endpoint&apos;s, and keeps the two clocks aligned. Without it, a sender at another sample rate is refused. At equal sample rates nothing changes.

It is optional because while it converts it uses CPU and a little latency, and the result is no longer bit-identical to what was sent.</source>
        <translation>다른 장치의 설정에 있는 Send: 줄이 이 장치로 보내는 소리의 샘플레이트가 이 장치의 샘플레이트와 다르면 변환하고, 두 장치의 클록 차이도 계속 보정합니다. 이 옵션을 끄면 샘플레이트가 다른 장치의 전송은 받지 않습니다. 샘플레이트가 같으면 달라지는 것은 없습니다.

이 옵션을 켜면 변환하는 동안 CPU를 사용하고 지연이 조금 늘어납니다. 변환한 오디오는 전송된 원본과 비트 단위로 같지 않습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="188"/>
        <source>Ignore silence in convolution</source>
        <translation>컨볼루션 무음 부분 무시</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="190"/>
        <source>Cuts the silent end off impulse responses that Convolution and MultiConvolution load. A long silent tail costs as much processing as sound does and can cause dropouts. Applies to every device.</source>
        <translation>Convolution과 MultiConvolution이 불러오는 임펄스 응답의 끝부분에서 무음을 잘라냅니다. 무음 부분도 소리가 있는 부분만큼 연산이 필요하므로 끝부분의 무음이 길면 소리가 끊길 수 있습니다. 모든 장치에 적용됩니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="71"/>
        <location filename="../DeviceSelector.cpp" line="115"/>
        <source>Playback devices</source>
        <translation>재생 장치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="76"/>
        <location filename="../DeviceSelector.cpp" line="119"/>
        <source>Capture devices</source>
        <translation>녹음 장치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="84"/>
        <location filename="../DeviceSelector.cpp" line="101"/>
        <location filename="../DeviceSelector.cpp" line="443"/>
        <location filename="../DeviceSelector.cpp" line="456"/>
        <source>Error while accessing the registry</source>
        <translation>레지스트리에 접근하는 중 오류가 발생했습니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="96"/>
        <location filename="../DeviceSelector.cpp" line="576"/>
        <source>Info</source>
        <translation>알림</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="96"/>
        <source>A registry value that is required for the operation of Equalizer APO was not set correctly. This might have been caused by a driver installation or uninstallation. The value has been corrected. A reboot may be required so that the changes can take effect.</source>
        <translation>Equalizer APO 동작에 필요한 레지스트리 값이 올바르게 설정되어 있지 않았습니다. 드라이버 설치나 제거 때문일 수 있습니다. 값을 바로잡았으며, 변경 사항을 적용하려면 재부팅이 필요할 수 있습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="552"/>
        <source>Upgrades available</source>
        <translation>업그레이드 가능</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="552"/>
        <source>The APO installation of some devices should be upgraded. Do you really want to cancel?</source>
        <translation>일부 장치의 APO 설치를 업그레이드해야 합니다. 정말 취소하시겠습니까?</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="576"/>
        <source>This dialog can be reopened anytime by launching Device Selector from the start menu.</source>
        <translation>이 창은 시작 메뉴에서 Device Selector를 실행해 언제든지 다시 열 수 있습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="582"/>
        <source>Reboot</source>
        <translation>재부팅</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="582"/>
        <source>To apply the changes, Windows should be rebooted. Reboot now?</source>
        <translation>변경 사항을 적용하려면 Windows를 재부팅해야 합니다. 지금 재부팅하시겠습니까?</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="165"/>
        <location filename="../DeviceSelector.cpp" line="760"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="102"/>
        <source>The registry repair did not finish. Reinstall Equalizer APO or see DeviceSelector.log for details.</source>
        <translation>레지스트리 복구를 끝내지 못했습니다. Equalizer APO를 다시 설치하거나 DeviceSelector.log에서 자세한 내용을 확인하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="164"/>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <source>Undoing the change did not finish either, so this device may be left partly changed. Reboot before trying again, and see the log for details.</source>
        <translation type="vanished">변경을 되돌리는 작업도 끝내지 못해 이 장치에 변경 사항이 일부 남아 있을 수 있습니다. 다시 시도하기 전에 재부팅하십시오. 자세한 내용은 로그를 확인하십시오.</translation>
    </message>
    <message>
        <source>The device was left as it was before.</source>
        <translation type="vanished">장치는 변경 전 상태를 유지합니다.</translation>
    </message>
    <message>
        <source>Details are in %1.</source>
        <translation type="vanished">자세한 내용은 %1에서 확인하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="765"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="940"/>
        <source>APO will be installed</source>
        <translation>APO를 설치합니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="943"/>
        <source>APO will be uninstalled</source>
        <translation>APO를 제거합니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="947"/>
        <source>APO will be upgraded</source>
        <translation>APO를 업그레이드합니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="949"/>
        <source>APO installation will be changed</source>
        <translation>APO 설치를 변경합니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="951"/>
        <source>Audio enhancements will be enabled</source>
        <translation>오디오 향상 기능을 켭니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="954"/>
        <source>APO is already installed</source>
        <translation>APO가 이미 설치되어 있습니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="954"/>
        <source>APO can be installed</source>
        <translation>APO를 설치할 수 있습니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="959"/>
        <source>Voicemeeter was uninstalled</source>
        <translation>Voicemeeter가 제거되었습니다</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="961"/>
        <source>Default device</source>
        <translation>기본 장치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="967"/>
        <source>Disabled</source>
        <translation>사용 안 함</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="969"/>
        <source>Unplugged</source>
        <translation>연결 끊김</translation>
    </message>
</context>
<context>
    <name>DeviceSelectorClass</name>
    <message>
        <location filename="../DeviceSelector.ui" line="176"/>
        <source>Use in ASIO apps</source>
        <translation>ASIO 앱에서 사용</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="173"/>
        <source>Adds `&lt;device&gt; - &lt;endpoint&gt; (EQ APO XT)` to the ASIO driver list. An application that picks it opens this device in WASAPI exclusive mode with the EQ applied.</source>
        <translation>ASIO 드라이버 목록에 `&lt;장치&gt; - &lt;엔드포인트&gt; (EQ APO XT)` 항목을 만듭니다. ASIO를 지원하는 앱에서 그 항목을 고르면 이 장치를 WASAPI 독점 모드로 열면서 EQ까지 적용합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="168"/>
        <source>Troubleshooting options (only use in case of problems)</source>
        <translation>문제 해결 옵션 (문제가 있을 때만 사용)</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="42"/>
        <source>Please select exactly one activated device by clicking on its name</source>
        <translation>이름을 클릭해 활성화된 장치를 하나만 선택하십시오</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="56"/>
        <location filename="../DeviceSelector.ui" line="86"/>
        <source>Install APO</source>
        <translation>APO 설치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="63"/>
        <location filename="../DeviceSelector.ui" line="144"/>
        <source>Use original APO</source>
        <translation>원본 APO 사용</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="76"/>
        <source>Pre-mix:</source>
        <translation>믹스 전:</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="115"/>
        <source>Post-mix:</source>
        <translation>믹스 후:</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="126"/>
        <source>Install as LFX/GFX</source>
        <translation>LFX/GFX로 설치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="131"/>
        <source>Install as SFX/MFX</source>
        <translation>SFX/MFX로 설치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="136"/>
        <source>Install as SFX/EFX</source>
        <translation>SFX/EFX로 설치</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="151"/>
        <source>Automatically adjust install mode if problems with current mode are detected</source>
        <translation>현재 모드에 문제가 감지되면 설치 모드를 자동으로 조정</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="154"/>
        <source>Auto:</source>
        <translation>자동:</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="161"/>
        <source>Allows to change silent input buffers to audible output buffers, so that delayed audio can be played.

CAUTION: Some sound card drivers can produce unwanted sounds when this is enabled.</source>
        <translation>무음 입력 버퍼를 소리가 나는 출력 버퍼로 바꿀 수 있게 하여, 지연된 오디오를 재생할 수 있습니다.

주의: 일부 사운드 카드 드라이버에서는 이 옵션을 켜면 원치 않는 소리가 날 수 있습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="166"/>
        <source>Allow silent buffer modification</source>
        <translation>무음 버퍼 수정 허용</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="183"/>
        <source>Plays what Send: lines in other endpoints&apos; configurations hand to this endpoint, even when no program plays here. EqualizerAPOHost keeps a silent stream open on it from logon, and silent buffer modification is turned on with it.</source>
        <translation>이 장치에서 재생하는 프로그램이 없어도 다른 장치의 설정에 있는 Send: 줄이 보내는 소리를 재생합니다. EqualizerAPOHost가 로그인할 때부터 이 장치에 무음 스트림을 열어 둡니다. 이 옵션을 켜면 무음 버퍼 수정도 허용합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="186"/>
        <source>Send</source>
        <translation>전송</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="217"/>
        <source>Remove the buffer</source>
        <translation>버퍼 제거</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="243"/>
        <source>Wait time:</source>
        <translation>대기 시간:</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="240"/>
        <location filename="../DeviceSelector.ui" line="250"/>
        <source>Sets how long to wait for processing once the buffer is removed. The longer the wait, the less sound without the EQ, but the more latency.</source>
        <translation>버퍼를 제거했을 때, 처리를 얼마나 기다릴지 정합니다. 길게 기다릴수록 EQ 없는 소리가 줄어들지만, 그만큼 지연이 늘어납니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="254"/>
        <source>Up to a quarter of the buffer</source>
        <translation>버퍼의 1/4까지</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="259"/>
        <source>Up to half of the buffer</source>
        <translation>버퍼의 절반까지</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="264"/>
        <source>Up to three quarters of the buffer</source>
        <translation>버퍼의 3/4까지</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="214"/>
        <source>Removing the buffer reduces latency. But when processing occasionally does not finish in time, sound without the EQ applied may come out for a moment. Applies to both input and output.</source>
        <translation>버퍼를 제거하면 지연이 줄어듭니다. 그러나 때때로 처리가 제때 끝나지 못하면, 순간적으로 EQ가 미적용된 소리가 출력될 수 있습니다. 입출력에 모두 적용됩니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="295"/>
        <source>Start the engine host automatically at boot</source>
        <translation>부팅 시 자동 시작</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="292"/>
        <source>Unchecked, the first application that opens the driver starts the host, and it leaves a minute after the last one closes it.</source>
        <translation>끄면 드라이버를 처음 여는 애플리케이션이 엔진 호스트를 띄우고, 마지막 애플리케이션이 드라이버를 닫은 뒤 1분이 지나면 호스트가 종료됩니다.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="321"/>
        <source>32-bit host support</source>
        <translation>32비트 호스트 지원</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="318"/>
        <source>Also registers the driver entry where 32-bit applications look for it. Unavailable when this build ships no 32-bit wrapper.</source>
        <translation>32비트 애플리케이션이 찾는 자리에도 드라이버 항목을 등록합니다. 32비트 래퍼가 없는 빌드에서는 쓸 수 없습니다.</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">커넥터</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">장치</translation>
    </message>
    <message>
        <source>Status</source>
        <translation type="vanished">상태</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="382"/>
        <source>Please select the devices for which Equalizer APO is to be installed:</source>
        <translation>Equalizer APO를 설치할 장치를 선택하십시오:</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="389"/>
        <source>Copy Device command to clipboard</source>
        <translation>Device 명령을 클립보드에 복사</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialog</name>
    <message>
        <source>Playback devices</source>
        <translation type="vanished">재생 장치</translation>
    </message>
    <message>
        <source>Capture devices</source>
        <translation type="vanished">녹음 장치</translation>
    </message>
    <message>
        <source>Error while accessing the registry</source>
        <translation type="vanished">레지스트리에 접근하는 중 오류가 발생했습니다</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is disabled</source>
        <translation type="vanished">장치가 사용 안 함 상태라 APO 설치를 테스트할 수 없습니다</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is unplugged</source>
        <translation type="vanished">장치가 연결 끊김 상태라 APO 설치를 테스트할 수 없습니다</translation>
    </message>
    <message>
        <source>Error</source>
        <translation type="vanished">오류</translation>
    </message>
    <message>
        <source>Equalizer APO works but original APO could not be initialized. Maybe unset &quot;Use original APO&quot; in troubleshooting options.</source>
        <translation type="vanished">Equalizer APO는 동작하지만 원본 APO를 초기화하지 못했습니다. 문제 해결 옵션에서 &quot;원본 APO 사용&quot;을 해제해 보십시오.</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialogClass</name>
    <message>
        <source>Testing APO installation on devices...</source>
        <translation type="vanished">장치에서 APO 설치를 테스트하는 중...</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">커넥터</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">장치</translation>
    </message>
    <message>
        <source>Pre-mix</source>
        <translation type="vanished">믹스 전</translation>
    </message>
    <message>
        <source>Post-mix</source>
        <translation type="vanished">믹스 후</translation>
    </message>
</context>
<context>
    <name>DeviceTestThread</name>
    <message>
        <location filename="../DeviceTestThread.cpp" line="82"/>
        <source>Could not initialize COM for device testing.</source>
        <translation>장치 테스트에 필요한 COM을 초기화하지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="83"/>
        <location filename="../DeviceTestThread.cpp" line="85"/>
        <source>COM initialization failed (0x%1).</source>
        <translation>COM을 초기화하지 못했습니다(0x%1).</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="112"/>
        <location filename="../DeviceTestThread.cpp" line="301"/>
        <source>Restarting audio service...</source>
        <translation>오디오 서비스를 다시 시작하는 중...</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="117"/>
        <location filename="../DeviceTestThread.cpp" line="306"/>
        <source>Restart failed.</source>
        <translation>다시 시작하지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="132"/>
        <source>Checking APO installation...</source>
        <translation>APO 설치를 확인하는 중...</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="142"/>
        <source>Could not prepare the device test.</source>
        <translation>장치 테스트를 준비하지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="104"/>
        <location filename="../DeviceTestThread.cpp" line="154"/>
        <source>Could not remove the device test registration: %1</source>
        <translation>장치 테스트 등록을 제거하지 못했습니다: %1</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="210"/>
        <source>Received unknown device GUID %1.</source>
        <translation>알 수 없는 장치 GUID %1을(를) 받았습니다.</translation>
    </message>
    <message numerus="yes">
        <location filename="../DeviceTestThread.cpp" line="233"/>
        <location filename="../DeviceTestView.cpp" line="514"/>
        <source>Check failed for %n device(s).</source>
        <translation>
            <numerusform>장치 %n개에서 확인에 실패했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="266"/>
        <source>Setting install mode for %1 %2 to %3.</source>
        <translation>%1 %2의 설치 모드를 %3(으)로 설정합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="297"/>
        <source>Trying other configurations...</source>
        <translation>다른 구성을 시도하는 중...</translation>
    </message>
    <message>
        <location filename="../DeviceTestThread.cpp" line="327"/>
        <source>Checks done. No problems were detected.</source>
        <translation>확인을 마쳤습니다. 문제가 발견되지 않았습니다.</translation>
    </message>
    <message numerus="yes">
        <location filename="../DeviceTestThread.cpp" line="329"/>
        <location filename="../DeviceTestView.cpp" line="515"/>
        <source>Checks done. Problems were detected for %n device(s).</source>
        <translation>
            <numerusform>확인을 마쳤습니다. 장치 %n개에서 문제가 발견되었습니다.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>DeviceTestView</name>
    <message>
        <location filename="../DeviceTestView.cpp" line="87"/>
        <source>APO installation check</source>
        <translation>APO 설치 확인</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="138"/>
        <source>Record</source>
        <translation>기록</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="154"/>
        <source>Stop</source>
        <translation>중지</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="157"/>
        <source>Reinstall</source>
        <translation>재설치</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="159"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <source>The cause could not be determined. Update the device&apos;s driver, then press Reinstall. Some drivers load no APO at all.</source>
        <translation type="vanished">원인은 알 수 없습니다. 장치 드라이버를 최신으로 바꾼 뒤 재설치하십시오. APO를 전혀 불러오지 않는 드라이버도 있습니다.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then press Reinstall.</source>
        <translation type="vanished">오디오 장치를 관리하는 프로그램을 닫은 뒤 재설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="263"/>
        <source>Not changed</source>
        <translation>바뀌지 않았습니다</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then try again.</source>
        <translation type="vanished">오디오 장치를 관리하는 프로그램을 닫은 뒤 다시 시도하십시오.</translation>
    </message>
    <message>
        <source>Restart the computer before trying again.</source>
        <translation type="vanished">컴퓨터를 다시 시작한 뒤 다시 시도하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="239"/>
        <source>All three install modes were tried and the APO was not installed.</source>
        <translation>설치 방식 세 가지를 모두 시도했지만 APO가 설치되지 않았습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="240"/>
        <source>The cause is not known. Update the device&apos;s driver, then reinstall. Some drivers load no APO at all.</source>
        <translation>원인은 알 수 없습니다. 장치 드라이버를 최신으로 바꾼 뒤 재설치하십시오. APO를 전혀 불러오지 않는 드라이버도 있습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="243"/>
        <source>The APO was not installed in the selected install mode (%1).</source>
        <translation>선택한 설치 방식(%1)으로는 APO가 설치되지 않았습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="244"/>
        <source>Select another install mode. The automatic install mode can be tried as well.</source>
        <translation>다른 설치 방식을 선택하십시오. 자동 설치 방식으로 시도해도 됩니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="246"/>
        <source>Post-mix does not work. Pre-mix works</source>
        <translation>믹스 후가 작동하지 않습니다. 믹스 전은 작동합니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="247"/>
        <source>The post-mix APO does not work. The pre-mix APO works.</source>
        <translation>믹스 후 APO가 작동하지 않습니다. 믹스 전 APO는 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="250"/>
        <source>Pre-mix does not work. Post-mix works</source>
        <translation>믹스 전이 작동하지 않습니다. 믹스 후는 작동합니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="251"/>
        <source>The pre-mix APO does not work. The post-mix APO works.</source>
        <translation>믹스 전 APO가 작동하지 않습니다. 믹스 후 APO는 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="254"/>
        <source>The original APO did not start. The APO works</source>
        <translation>원본 APO가 시작되지 않았습니다. APO는 작동합니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="255"/>
        <source>The APO of the device&apos;s maker did not start, so the device&apos;s own sound effects are missing. The EQ APO works.</source>
        <translation>제조사의 APO가 시작되지 않으므로, 장치의 음향 효과는 누락됩니다. EQ APO는 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="260"/>
        <source>The install in another mode was refused by Windows. The install was stopped.</source>
        <translation>다른 방식으로 설치하려 했지만 Windows가 변경을 거부했습니다. 설치가 중지되었습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="261"/>
        <location filename="../DeviceTestView.cpp" line="265"/>
        <source>Close programs that manage audio devices, then reinstall.</source>
        <translation>오디오 장치를 관리하는 프로그램을 닫은 뒤 재설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="264"/>
        <source>Windows refused the change to this device. The install was stopped.</source>
        <translation>Windows가 이 장치의 변경을 거부했습니다. 설치가 중지되었습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="267"/>
        <source>Undo failed</source>
        <translation>되돌리기 실패</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="268"/>
        <source>Windows refused the change, and undoing it failed as well.</source>
        <translation>Windows가 변경을 거부했고, 되돌리기도 실패했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="269"/>
        <location filename="../DeviceTestView.cpp" line="289"/>
        <source>Restart the computer, then reinstall.</source>
        <translation>컴퓨터를 다시 시작한 뒤 재설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="272"/>
        <source>This device is disabled in Windows, so the APO cannot be installed on it.</source>
        <translation>이 장치는 Windows에서 사용 안함으로 설정되어 있어 APO를 설치할 수 없습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="273"/>
        <source>Enable the device.</source>
        <translation>장치의 사용 안함 상태를 해제하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="276"/>
        <source>Nothing is plugged into this device, so the APO cannot be installed on it.</source>
        <translation>장치가 연결되어 있지 않아 APO를 설치할 수 없습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="277"/>
        <source>Plug the device in.</source>
        <translation>장치를 연결하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="281"/>
        <location filename="../DeviceTestView.cpp" line="297"/>
        <location filename="../DeviceTestView.cpp" line="301"/>
        <source>Reinstall.</source>
        <translation>재설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="284"/>
        <source>Windows audio could not be restarted. The EQ APO works only after Windows audio restarts.</source>
        <translation>Windows 오디오를 다시 시작하지 못했습니다. EQ APO는 Windows 오디오를 다시 시작해야 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="288"/>
        <source>A Windows error kept the installation check from starting.</source>
        <translation>Windows 오류로 설치 확인을 시작하지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="292"/>
        <source>The EQ APO install was refused.</source>
        <translation>EQ APO 설치가 거부되었습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="293"/>
        <source>Close security programs that block changes to system settings, then reinstall.</source>
        <translation>시스템 설정 변경을 막는 보안 프로그램을 닫은 뒤 재설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="296"/>
        <source>The connection to the APO was lost.</source>
        <translation>APO와의 연결이 끊겼습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="300"/>
        <source>The list of audio devices could not be read, so no device was checked.</source>
        <translation>오디오 장치 목록을 읽지 못해 어느 장치도 확인하지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="394"/>
        <source>Checking that the APO works on each device (%1 of %2)</source>
        <translation>장치마다 APO가 작동하는지 확인하는 중입니다 (%1/%2)</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="396"/>
        <source>Restarting Windows audio</source>
        <translation>Windows 오디오를 다시 시작하는 중입니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="397"/>
        <source>Windows audio restarts during the check, so sound stops for a few seconds.</source>
        <translation>확인하는 동안 Windows 오디오를 다시 시작하므로 소리가 몇 초 끊깁니다.</translation>
    </message>
    <message numerus="yes">
        <location filename="../DeviceTestView.cpp" line="410"/>
        <source>%n device(s) could not be changed.</source>
        <translation>
            <numerusform>장치 %n개를 바꾸지 못했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="412"/>
        <source>The APO works on the devices that were checked.</source>
        <translation>확인한 장치에서는 APO가 작동합니다.</translation>
    </message>
    <message numerus="yes">
        <source>The APO works. %n device(s) have something to look at.</source>
        <translation type="vanished">
            <numerusform>장치 %n개에 확인할 것이 있습니다. APO는 작동합니다.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="422"/>
        <source>The APO is not installed on any device.</source>
        <translation>APO가 설치된 장치가 없습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="427"/>
        <source>The APO works on every device that could be checked.</source>
        <translation>확인할 수 있는 모든 장치에서 APO가 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="428"/>
        <source>No device could be checked.</source>
        <translation>확인할 수 있는 장치가 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location filename="../DeviceTestView.cpp" line="429"/>
        <source>%n device(s) could not be checked.</source>
        <translation>
            <numerusform>장치 %n개는 확인하지 못했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="434"/>
        <source>The APO works on every device.</source>
        <translation>모든 장치에서 APO가 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="435"/>
        <source>The equalizer is applied from now on.</source>
        <translation>지금부터 이퀄라이저가 적용됩니다.</translation>
    </message>
    <message>
        <source>Open the troubleshooting options in the Device Selector and choose another install mode. The record below has the details.</source>
        <translation type="vanished">Device Selector의 문제 해결 옵션에서 설치 방식을 바꿔 보십시오. 자세한 내용은 아래 기록에 있습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="336"/>
        <location filename="../DeviceTestDialog.cpp" line="378"/>
        <location filename="../DeviceTestView.cpp" line="467"/>
        <source>The APO works</source>
        <translation>APO가 작동합니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="155"/>
        <location filename="../DeviceTestDialog.cpp" line="359"/>
        <location filename="../DeviceTestView.cpp" line="468"/>
        <source>Checking</source>
        <translation>확인하는 중입니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="75"/>
        <source>Reinstalling %1 %2</source>
        <translation>%1 %2 재설치 중</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="168"/>
        <location filename="../DeviceTestDialog.cpp" line="581"/>
        <source>Details are in %1.</source>
        <translation>자세한 내용은 %1에 있습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="344"/>
        <source>Trying another install mode</source>
        <translation>다른 설치 방식을 시도하는 중입니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="349"/>
        <location filename="../DeviceTestView.cpp" line="479"/>
        <source>Pre-mix works. Checking post-mix</source>
        <translation>믹스 전은 작동합니다. 믹스 후를 확인하는 중입니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="354"/>
        <source>Post-mix works. Checking pre-mix</source>
        <translation>믹스 후는 작동합니다. 믹스 전을 확인하는 중입니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="606"/>
        <source>Install undone</source>
        <translation>설치 되돌림</translation>
    </message>
    <message>
        <source>The APO works, but the original APO could not start</source>
        <translation type="vanished">원본 APO가 시작되지 않았습니다. APO는 작동합니다</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="238"/>
        <location filename="../DeviceTestView.cpp" line="242"/>
        <source>The APO does not work</source>
        <translation>APO가 작동하지 않습니다</translation>
    </message>
    <message>
        <source>Enable the device, then press Reinstall.</source>
        <translation type="vanished">장치의 사용 안함 상태를 해제하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="275"/>
        <source>Not checked: the device is unplugged</source>
        <translation>연결이 끊겨 확인하지 못했습니다</translation>
    </message>
    <message>
        <source>Plug it in, then press Reinstall.</source>
        <translation type="vanished">장치를 연결하십시오.</translation>
    </message>
    <message>
        <source>Press Reinstall to try again.</source>
        <translation type="vanished">재설치하십시오.</translation>
    </message>
    <message>
        <source>Close security programs that block changes to system settings, then press Reinstall.</source>
        <translation type="vanished">시스템 설정 변경을 막는 보안 프로그램을 닫은 뒤 재설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="405"/>
        <source>The other devices are not affected.</source>
        <translation>다른 장치에는 영향이 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location filename="../DeviceTestView.cpp" line="417"/>
        <source>%n device(s) have something to look at. The APO works.</source>
        <translation><numerusform>장치 %n개에 확인할 것이 있습니다. APO는 작동합니다.</numerusform></translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="440"/>
        <source>The install was stopped.</source>
        <translation>설치를 중지했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="444"/>
        <source>The check did not finish.</source>
        <translation>확인을 끝내지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="543"/>
        <location filename="../DeviceTestDialog.cpp" line="629"/>
        <location filename="../DeviceTestView.cpp" line="529"/>
        <source>Not checked</source>
        <translation>확인하지 못했습니다</translation>
    </message>
    <message>
        <source>All three install modes were tried and Windows did not load the APO on this device in any of them.</source>
        <translation type="vanished">설치 방식 세 가지를 모두 시도했지만 APO가 설치되지 않았습니다.</translation>
    </message>
    <message>
        <source>The cause could not be determined. Update the device&apos;s driver and check again; some drivers load no APO at all.</source>
        <translation type="vanished">원인은 알 수 없습니다. 장치 드라이버를 최신으로 바꾼 뒤 다시 확인하십시오. APO를 전혀 불러오지 않는 드라이버도 있습니다.</translation>
    </message>
    <message>
        <source>Windows did not load the APO on this device in the selected install mode (%1).</source>
        <translation type="vanished">선택한 설치 방식(%1)으로는 APO가 설치되지 않았습니다.</translation>
    </message>
    <message>
        <source>Turn on Automatic in the troubleshooting options, and the check tries the other modes too.</source>
        <translation type="vanished">다른 설치 방식을 선택하십시오. 자동 설치 방식으로 시도해도 됩니다.</translation>
    </message>
    <message>
        <source>Pre-mix works, post-mix does not</source>
        <translation type="vanished">믹스 후가 작동하지 않습니다. 믹스 전은 작동합니다</translation>
    </message>
    <message>
        <source>The pre-mix APO works. Windows did not load the post-mix APO on this device.</source>
        <translation type="vanished">믹스 후 APO가 작동하지 않습니다. 믹스 전 APO는 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="248"/>
        <source>Filters of the post-mix stage are not applied. Turn off post-mix install for this device, or try another install mode.</source>
        <translation>믹스 후 단계의 필터는 적용되지 않습니다. 이 장치의 믹스 후 설치를 끄거나 다른 설치 방식을 시도하십시오.</translation>
    </message>
    <message>
        <source>Post-mix works, pre-mix does not</source>
        <translation type="vanished">믹스 전이 작동하지 않습니다. 믹스 후는 작동합니다</translation>
    </message>
    <message>
        <source>The post-mix APO works. Windows did not load the pre-mix APO on this device.</source>
        <translation type="vanished">믹스 전 APO가 작동하지 않습니다. 믹스 후 APO는 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="252"/>
        <source>Filters of the pre-mix stage are not applied. Try another install mode.</source>
        <translation>믹스 전 단계의 필터는 적용되지 않습니다. 다른 설치 방식을 시도하십시오.</translation>
    </message>
    <message>
        <source>The equalizer works. The APO of the device&apos;s maker, which runs beside it, did not start, so that maker&apos;s effects are missing.</source>
        <translation type="vanished">제조사의 APO가 시작되지 않으므로, 장치의 음향 효과는 누락됩니다. EQ APO는 작동합니다.</translation>
    </message>
    <message>
        <source>If you do not use those effects, turn off Use original APO in the troubleshooting options.</source>
        <translation type="vanished">그 효과를 쓰지 않는다면 문제 해결 옵션에서 원본 APO 사용을 끄십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="259"/>
        <source>The install mode could not be changed</source>
        <translation>설치 방식을 바꾸지 못했습니다</translation>
    </message>
    <message>
        <source>The check tried to install in another mode and Windows refused the change. The device was left as it was before.</source>
        <translation type="vanished">다른 방식으로 설치하려 했지만 Windows가 변경을 거부했습니다. 설치가 중지되었습니다.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then check again.</source>
        <translation type="vanished">오디오 장치를 관리하는 프로그램을 닫은 뒤 다시 확인하십시오.</translation>
    </message>
    <message>
        <source>Not installed</source>
        <translation type="vanished">설치하지 못했습니다</translation>
    </message>
    <message>
        <source>Windows refused the change to this device. The device was left as it was before.</source>
        <translation type="vanished">Windows가 이 장치의 변경을 거부했습니다. 설치가 중지되었습니다.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then install again.</source>
        <translation type="vanished">오디오 장치를 관리하는 프로그램을 닫은 뒤 다시 설치하십시오.</translation>
    </message>
    <message>
        <source>Partly changed</source>
        <translation type="vanished">되돌리기 실패</translation>
    </message>
    <message>
        <source>Windows refused the change, and undoing it did not finish either. The device may be left partly changed.</source>
        <translation type="vanished">Windows가 변경을 거부했고, 되돌리기도 실패했습니다.</translation>
    </message>
    <message>
        <source>Restart the computer before installing again.</source>
        <translation type="vanished">컴퓨터를 다시 시작한 뒤 다시 설치하십시오.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="271"/>
        <source>Not checked: the device is disabled</source>
        <translation>사용 안 함 상태라 확인하지 못했습니다</translation>
    </message>
    <message>
        <source>The device is disabled in Windows, so nothing could be played through it.</source>
        <translation type="vanished">이 장치는 Windows에서 사용 안함으로 설정되어 있어 APO를 설치할 수 없습니다.</translation>
    </message>
    <message>
        <source>Enable the device, then open the Device Selector and press OK to check again.</source>
        <translation type="vanished">장치를 사용으로 바꾼 뒤 Device Selector를 열고 확인을 누르면 다시 확인합니다.</translation>
    </message>
    <message>
        <source>Nothing is plugged into this device, so nothing could be played through it.</source>
        <translation type="vanished">장치가 연결되어 있지 않아 APO를 설치할 수 없습니다.</translation>
    </message>
    <message>
        <source>Plug it in, then open the Device Selector and press OK to check again.</source>
        <translation type="vanished">연결한 뒤 Device Selector를 열고 확인을 누르면 다시 확인합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="279"/>
        <source>Not checked: the device could not be opened</source>
        <translation>확인하지 못함: 장치를 열 수 없음</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="280"/>
        <source>Nothing could be played through this device, so the APO on it could not answer.</source>
        <translation>이 장치로 소리를 보낼 수 없어 APO의 응답을 받지 못했습니다.</translation>
    </message>
    <message>
        <source>Windows audio could not be restarted, so the check did not run. The APO is installed, but it starts working only after Windows audio restarts.</source>
        <translation type="vanished">Windows 오디오를 다시 시작하지 못했습니다. EQ APO는 Windows 오디오를 다시 시작해야 작동합니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="285"/>
        <source>Restart the computer.</source>
        <translation>컴퓨터를 다시 시작하십시오.</translation>
    </message>
    <message>
        <source>A Windows component the check needs did not start, so the check did not run.</source>
        <translation type="vanished">확인에 필요한 Windows 기능이 시작되지 않아 확인하지 못했습니다.</translation>
    </message>
    <message>
        <source>Restart the computer, then open the Device Selector and press OK.</source>
        <translation type="vanished">컴퓨터를 다시 시작한 뒤 재설치하십시오.</translation>
    </message>
    <message>
        <source>Windows blocked the setting the check uses to hear from the APO, so the check did not run.</source>
        <translation type="vanished">APO의 응답을 받는 데 필요한 설정을 Windows가 막아 확인하지 못했습니다.</translation>
    </message>
    <message>
        <source>Close security programs that block changes to system settings, then open the Device Selector and press OK.</source>
        <translation type="vanished">시스템 설정 변경을 막는 보안 프로그램을 닫은 뒤 Device Selector를 열고 확인을 누르십시오.</translation>
    </message>
    <message>
        <source>The check lost its connection to the APO before every device had answered.</source>
        <translation type="vanished">모든 장치의 응답을 받기 전에 APO와의 연결이 끊겼습니다.</translation>
    </message>
    <message>
        <source>Open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Device Selector를 열고 확인을 누르면 다시 확인합니다.</translation>
    </message>
    <message>
        <source>The check was stopped. The APO is installed, but whether it works was not checked.</source>
        <translation type="vanished">확인을 중지했습니다. APO는 설치되었지만 작동하는지는 확인하지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="445"/>
        <source>Whether the APO works is not known.</source>
        <translation>APO가 작동하는지는 알 수 없습니다.</translation>
    </message>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="425"/>
        <location filename="../DeviceTestDialog.cpp" line="590"/>
        <location filename="../DeviceTestView.cpp" line="537"/>
        <source>Windows audio</source>
        <translation>Windows 오디오</translation>
    </message>
    <message numerus="yes">
        <location filename="../DeviceTestView.cpp" line="403"/>
        <source>The APO does not work on %n device(s).</source>
        <translation>
            <numerusform>장치 %n개에서 APO가 작동하지 않습니다.</numerusform>
        </translation>
    </message>
</context>
</TS>
