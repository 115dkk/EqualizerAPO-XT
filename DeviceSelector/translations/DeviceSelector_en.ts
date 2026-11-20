<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en">
<context>
    <name>DeviceSelector</name>
    <message>
        <location filename="../DeviceSelector.cpp" line="+195"/>
        <source>Multi-core convolution processing</source>
        <translation>Multi-core convolution processing</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Spreads the work of Convolution and MultiConvolution over several CPU cores, so long impulse responses on many channels no longer load a single core. The sound and the latency stay the same.

It is optional because it starts extra audio threads. Turn it off if it causes dropouts on your system. Applies to every device.</source>
        <translation>Spreads the work of Convolution and MultiConvolution over several CPU cores, so long impulse responses on many channels no longer load a single core. The sound and the latency stay the same.

It is optional because it starts extra audio threads. Turn it off if it causes dropouts on your system. Applies to every device.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Resample received audio</source>
        <translation>Resample received audio</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Converts audio that Send: lines in other endpoints&apos; configurations hand to this endpoint when their sample rate differs from this endpoint&apos;s, and keeps the two clocks aligned. Without it, a sender at another sample rate is refused. At equal sample rates nothing changes.

It is optional because while it converts it uses CPU and a little latency, and the result is no longer bit-identical to what was sent.</source>
        <translation>Converts audio that Send: lines in other endpoints&apos; configurations hand to this endpoint when their sample rate differs from this endpoint&apos;s, and keeps the two clocks aligned. Without it, a sender at another sample rate is refused. At equal sample rates nothing changes.

It is optional because while it converts it uses CPU and a little latency, and the result is no longer bit-identical to what was sent.</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Ignore silence in convolution</source>
        <translation>Ignore silence in convolution</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cuts the silent end off impulse responses that Convolution and MultiConvolution load. A long silent tail costs as much processing as sound does and can cause dropouts. Applies to every device.</source>
        <translation>Cuts the silent end off impulse responses that Convolution and MultiConvolution load. A long silent tail costs as much processing as sound does and can cause dropouts. Applies to every device.</translation>
    </message>
    <message>
        <location line="-119"/>
        <location line="+44"/>
        <source>Playback devices</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+43"/>
        <source>Capture devices</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-35"/>
        <location line="+17"/>
        <location line="+342"/>
        <location line="+13"/>
        <source>Error while accessing the registry</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-360"/>
        <location line="+462"/>
        <source>Info</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-462"/>
        <source>A registry value that is required for the operation of Equalizer APO was not set correctly. This might have been caused by a driver installation or uninstallation. The value has been corrected. A reboot may be required so that the changes can take effect.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The registry repair did not finish. Reinstall Equalizer APO or see DeviceSelector.log for details.</source>
        <translation>The registry repair did not finish. Reinstall Equalizer APO or see DeviceSelector.log for details.</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>OK</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+577"/>
        <source>Cancel</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Undoing the change did not finish either, so this device may be left partly changed. Reboot before trying again, and see the log for details.</source>
        <translation type="vanished">Undoing the change did not finish either, so this device may be left partly changed. Reboot before trying again, and see the log for details.</translation>
    </message>
    <message>
        <source>The device was left as it was before.</source>
        <translation type="vanished">The device was left as it was before.</translation>
    </message>
    <message>
        <source>Details are in %1.</source>
        <translation type="vanished">Details are in %1.</translation>
    </message>
    <message>
        <location line="-208"/>
        <source>Upgrades available</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The APO installation of some devices should be upgraded. Do you really want to cancel?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+24"/>
        <source>This dialog can be reopened anytime by launching Device Selector from the start menu.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Reboot</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+0"/>
        <source>To apply the changes, Windows should be rebooted. Reboot now?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+183"/>
        <source>Close</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+175"/>
        <source>APO will be installed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>APO will be uninstalled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <source>APO will be upgraded</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>APO installation will be changed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Audio enhancements will be enabled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>APO is already installed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+0"/>
        <source>APO can be installed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Voicemeeter was uninstalled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Default device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disabled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unplugged</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DeviceSelectorClass</name>
    <message>
        <location filename="../DeviceSelector.ui" line="+176"/>
        <source>Use in ASIO apps</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Adds `&lt;device&gt; - &lt;endpoint&gt; (EQ APO XT)` to the ASIO driver list. An application that picks it opens this device in WASAPI exclusive mode with the EQ applied.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-131"/>
        <source>Please select exactly one activated device by clicking on its name</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+30"/>
        <source>Install APO</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+81"/>
        <source>Use original APO</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Pre-mix:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Post-mix:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Install as LFX/GFX</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/MFX</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/EFX</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Automatically adjust install mode if problems with current mode are detected</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Allows to change silent input buffers to audible output buffers, so that delayed audio can be played.

CAUTION: Some sound card drivers can produce unwanted sounds when this is enabled.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allow silent buffer modification</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Plays what Send: lines in other endpoints&apos; configurations hand to this endpoint, even when no program plays here. EqualizerAPOHost keeps a silent stream open on it from logon, and silent buffer modification is turned on with it.</source>
        <translation>Plays what Send: lines in other endpoints&apos; configurations hand to this endpoint, even when no program plays here. EqualizerAPOHost keeps a silent stream open on it from logon, and silent buffer modification is turned on with it.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>Send</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Removing the buffer reduces latency. But when processing occasionally does not finish in time, sound without the EQ applied may come out for a moment. Applies to both input and output.</source>
        <translation>Removing the buffer reduces latency. But when processing occasionally does not finish in time, sound without the EQ applied may come out for a moment. Applies to both input and output.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove the buffer</source>
        <translation>Remove the buffer</translation>
    </message>
    <message>
        <location line="+23"/>
        <location line="+10"/>
        <source>Sets how long to wait for processing once the buffer is removed. The longer the wait, the less sound without the EQ, but the more latency.</source>
        <translation>Sets how long to wait for processing once the buffer is removed. The longer the wait, the less sound without the EQ, but the more latency.</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Wait time:</source>
        <translation>Wait time:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Up to a quarter of the buffer</source>
        <translation>Up to a quarter of the buffer</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to half of the buffer</source>
        <translation>Up to half of the buffer</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to three quarters of the buffer</source>
        <translation>Up to three quarters of the buffer</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Unchecked, the first application that opens the driver starts the host, and it leaves a minute after the last one closes it.</source>
        <translation>Unchecked, the first application that opens the driver starts the host, and it leaves a minute after the last one closes it.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start the engine host automatically at boot</source>
        <translation>Start the engine host automatically at boot</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Also registers the driver entry where 32-bit applications look for it. Unavailable when this build ships no 32-bit wrapper.</source>
        <translation>Also registers the driver entry where 32-bit applications look for it. Unavailable when this build ships no 32-bit wrapper.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>32-bit host support</source>
        <translation>32-bit host support</translation>
    </message>
    <message>
        <location line="+61"/>
        <source>Please select the devices for which Equalizer APO is to be installed:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Copy Device command to clipboard</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="-783"/>
        <source>Troubleshooting options (only use in case of problems)</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DeviceTestThread</name>
    <message>
        <location filename="../DeviceTestThread.cpp" line="+82"/>
        <source>Could not initialize COM for device testing.</source>
        <translation>Could not initialize COM for device testing.</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+2"/>
        <source>COM initialization failed (0x%1).</source>
        <translation>COM initialization failed (0x%1).</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+190"/>
        <source>Restarting audio service...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-185"/>
        <location line="+190"/>
        <source>Restart failed.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-175"/>
        <source>Checking APO installation...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Could not prepare the device test.</source>
        <translation>Could not prepare the device test.</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+50"/>
        <source>Could not remove the device test registration: %1</source>
        <translation>Could not remove the device test registration: %1</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Received unknown device GUID %1.</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <location filename="../DeviceTestView.cpp" line="+512"/>
        <source>Check failed for %n device(s).</source>
        <translation>
            <numerusform>Check failed for %n device.</numerusform>
            <numerusform>Check failed for %n devices.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Setting install mode for %1 %2 to %3.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Trying other configurations...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Checks done. No problems were detected.</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location filename="../DeviceTestView.cpp" line="+1"/>
        <source>Checks done. Problems were detected for %n device(s).</source>
        <translation>
            <numerusform>Checks done. Problems were detected for %n device.</numerusform>
            <numerusform>Checks done. Problems were detected for %n devices.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>DeviceTestView</name>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="+71"/>
        <source>Reinstalling %1 %2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+80"/>
        <location line="+204"/>
        <location filename="../DeviceTestView.cpp" line="-47"/>
        <source>Checking</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-191"/>
        <source>Details are in %1.</source>
        <translation type="unfinished">Details are in %1.</translation>
    </message>
    <message>
        <location line="+168"/>
        <location line="+42"/>
        <location filename="../DeviceTestView.cpp" line="-1"/>
        <source>The APO works</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Trying another install mode</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../DeviceTestView.cpp" line="+12"/>
        <source>Pre-mix works. Checking post-mix</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Post-mix works. Checking pre-mix</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+71"/>
        <location filename="../DeviceTestView.cpp" line="+58"/>
        <source>Windows audio</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+82"/>
        <location filename="../DeviceTestView.cpp" line="-8"/>
        <source>Not checked</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="-440"/>
        <source>APO installation check</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Record</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Stop</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reinstall</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Close</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+79"/>
        <location line="+4"/>
        <source>The APO does not work</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-3"/>
        <source>All three install modes were tried and Windows did not load the APO on this device in any of them.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Windows did not load the APO on this device in the selected install mode (%1).</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Turn on Automatic in the troubleshooting options, and the check tries the other modes too.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pre-mix works, post-mix does not</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The pre-mix APO works. Windows did not load the post-mix APO on this device.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filters of the post-mix stage are not applied. Turn off post-mix install for this device, or try another install mode.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-mix works, pre-mix does not</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The post-mix APO works. Windows did not load the pre-mix APO on this device.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filters of the pre-mix stage are not applied. Try another install mode.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The APO works, but the original APO could not start</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The equalizer works. The APO of the device&apos;s maker, which runs beside it, did not start, so that maker&apos;s effects are missing.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>If you do not use those effects, turn off Use original APO in the troubleshooting options.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The install mode could not be changed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The check tried to install in another mode and Windows refused the change. The device was left as it was before.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not changed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change to this device. The device was left as it was before.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Partly changed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change, and undoing it did not finish either. The device may be left partly changed.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not checked: the device is disabled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The device is disabled in Windows, so nothing could be played through it.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not checked: the device is unplugged</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing is plugged into this device, so nothing could be played through it.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not checked: the device could not be opened</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing could be played through this device, so the APO on it could not answer.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Windows audio could not be restarted, so the check did not run. The APO is installed, but it starts working only after Windows audio restarts.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restart the computer.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>A Windows component the check needs did not start, so the check did not run.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+20"/>
        <source>Restart the computer, then open the Device Selector and press OK.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-48"/>
        <source>The cause could not be determined. Update the device&apos;s driver, then press Reinstall. Some drivers load no APO at all.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+4"/>
        <source>Close programs that manage audio devices, then press Reinstall.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Enable the device, then press Reinstall.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Plug it in, then press Reinstall.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+16"/>
        <location line="+4"/>
        <location line="+4"/>
        <source>Press Reinstall to try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Windows blocked the setting the check uses to hear from the APO, so the check did not run.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close security programs that block changes to system settings, then press Reinstall.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The check lost its connection to the APO before every device had answered.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The list of audio devices could not be read, so no device was checked.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The check was stopped. The APO is installed, but whether it works was not checked.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Checking that the APO works on each device (%1 of %2)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Restarting Windows audio</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows audio restarts during the check, so sound stops for a few seconds.</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>The APO does not work on %n device(s).</source>
        <translation type="unfinished">
            <numerusform></numerusform>
            <numerusform></numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The other devices are not affected.</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>%n device(s) could not be changed.</source>
        <translation type="unfinished">
            <numerusform></numerusform>
            <numerusform></numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The APO works on the devices that were checked.</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>The APO works. %n device(s) have something to look at.</source>
        <translation type="unfinished">
            <numerusform></numerusform>
            <numerusform></numerusform>
        </translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO is not installed on any device.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO works on every device that could be checked.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No device could be checked.</source>
        <translation type="unfinished"></translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>%n device(s) could not be checked.</source>
        <translation type="unfinished">
            <numerusform></numerusform>
            <numerusform></numerusform>
        </translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO works on every device.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The equalizer is applied from now on.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The check did not finish.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Whether the APO works is not known.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
