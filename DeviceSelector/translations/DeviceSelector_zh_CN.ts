<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>DeviceSelector</name>
    <message>
        <location filename="../DeviceSelector.cpp" line="+195"/>
        <source>Multi-core convolution processing</source>
        <translation>多核卷积处理</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Spreads the work of Convolution and MultiConvolution over several CPU cores, so long impulse responses on many channels no longer load a single core. The sound and the latency stay the same.

It is optional because it starts extra audio threads. Turn it off if it causes dropouts on your system. Applies to every device.</source>
        <translation>将 Convolution 和 MultiConvolution 的计算分配到多个 CPU 核心，使多个通道的长脉冲响应不再由单个核心处理。声音和延迟保持不变。

此功能可选择是否启用，因为它会启动额外的音频线程。如果启用后在您的电脑上导致声音断续，请关闭此选项。此设置适用于所有设备。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Resample received audio</source>
        <translation>对接收的音频重采样</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Converts audio that Send: lines in other endpoints&apos; configurations hand to this endpoint when their sample rate differs from this endpoint&apos;s, and keeps the two clocks aligned. Without it, a sender at another sample rate is refused. At equal sample rates nothing changes.

It is optional because while it converts it uses CPU and a little latency, and the result is no longer bit-identical to what was sent.</source>
        <translation>当其他端点的采样率与此端点不同时，转换这些端点通过配置中的 Send: 行发送到此端点的音频，并保持两端时钟同步。未启用此选项时，会拒绝采样率不同的发送端。采样率相同时没有任何变化。

此功能可选择是否启用，因为转换时会占用 CPU，增加少量延迟，且转换结果不再与发送的原始音频逐位一致。</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Ignore silence in convolution</source>
        <translation>忽略卷积中的静音</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cuts the silent end off impulse responses that Convolution and MultiConvolution load. A long silent tail costs as much processing as sound does and can cause dropouts. Applies to every device.</source>
        <translation>去除 Convolution 和 MultiConvolution 加载的脉冲响应末尾的静音部分。处理静音与处理有声部分所需的计算量相同，因此末尾的静音过长可能导致声音断续。此设置适用于所有设备。</translation>
    </message>
    <message>
        <location line="-119"/>
        <location line="+44"/>
        <source>Playback devices</source>
        <translation>播放设备</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+43"/>
        <source>Capture devices</source>
        <translation>捕获设备</translation>
    </message>
    <message>
        <location line="-35"/>
        <location line="+17"/>
        <location line="+342"/>
        <location line="+13"/>
        <source>Error while accessing the registry</source>
        <translation>访问注册表时出错</translation>
    </message>
    <message>
        <location line="-360"/>
        <location line="+448"/>
        <source>Info</source>
        <translation>信息</translation>
    </message>
    <message>
        <location line="-448"/>
        <source>A registry value that is required for the operation of Equalizer APO was not set correctly. This might have been caused by a driver installation or uninstallation. The value has been corrected. A reboot may be required so that the changes can take effect.</source>
        <translation>未正确设置 Equalizer APO 操作所需的注册表值。这可能是由驱动程序安装或卸载引起的。该值已更正。可能需要重新启动才能使更改生效。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The registry repair did not finish. Reinstall Equalizer APO or see DeviceSelector.log for details.</source>
        <translation>注册表修复未完成。请重新安装 Equalizer APO，或查看 DeviceSelector.log 了解详情。</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>OK</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Undoing the change did not finish either, so this device may be left partly changed. Reboot before trying again, and see the log for details.</source>
        <translation type="vanished">撤销更改也未能完成，因此此设备可能仍保留部分更改。请重新启动后再试，详情请查看日志。</translation>
    </message>
    <message>
        <source>The device was left as it was before.</source>
        <translation type="vanished">设备保持更改前的状态。</translation>
    </message>
    <message>
        <source>Details are in %1.</source>
        <translation type="vanished">详情请查看 %1。</translation>
    </message>
    <message>
        <location line="+747"/>
        <source>APO will be uninstalled</source>
        <translation>APO 将被卸载</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>APO will be upgraded</source>
        <translation>APO 将被升级</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>APO installation will be changed</source>
        <translation>APO 安装将会更改</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Audio enhancements will be enabled</source>
        <translation>音频增强功能将会启用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>APO is already installed</source>
        <translation>APO 已安装</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>APO can be installed</source>
        <translation>可以安装 APO</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Voicemeeter was uninstalled</source>
        <translation>Voicemeeter 已卸载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Default device</source>
        <translation>默认设备</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disabled</source>
        <translation>禁用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unplugged</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="-417"/>
        <source>Upgrades available</source>
        <translation>更新可用</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The APO installation of some devices should be upgraded. Do you really want to cancel?</source>
        <translation>部分设备的 APO 安装需要升级。确实要取消吗？</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Reboot</source>
        <translation>重新启动</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>To apply the changes, Windows should be rebooted. Reboot now?</source>
        <translation>要应用更改，应重新启动 Windows。立即重新启动？</translation>
    </message>
    <message>
        <location line="+358"/>
        <source>APO will be installed</source>
        <translation>将安装 APO</translation>
    </message>
    <message>
        <location line="-743"/>
        <location line="+563"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="-189"/>
        <source>This dialog can be reopened anytime by launching Device Selector from the start menu.</source>
        <translation>可以通过从开始菜单启动设备选择器随时重新打开此对话框。</translation>
    </message>
</context>
<context>
    <name>DeviceSelectorClass</name>
    <message>
        <location filename="../DeviceSelector.ui" line="+176"/>
        <source>Use in ASIO apps</source>
        <translation>在 ASIO 应用中使用</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Adds `&lt;device&gt; - &lt;endpoint&gt; (EQ APO XT)` to the ASIO driver list. An application that picks it opens this device in WASAPI exclusive mode with the EQ applied.</source>
        <translation>在 ASIO 驱动列表中添加“&lt;设备&gt; - &lt;端点&gt; (EQ APO XT)”。选择该条目的应用程序会以 WASAPI 独占模式打开此设备并应用均衡器。</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">连接器</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">设备</translation>
    </message>
    <message>
        <source>Status</source>
        <translation type="vanished">状态</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="-376"/>
        <source>Troubleshooting options (only use in case of problems)</source>
        <translation>故障排除选项（仅在出现问题时使用）</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="-110"/>
        <location line="+81"/>
        <source>Use original APO</source>
        <translation>使用原始 APO</translation>
    </message>
    <message>
        <location line="-88"/>
        <location line="+30"/>
        <source>Install APO</source>
        <translation>安装 APO</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Post-mix:</source>
        <translation>混合后：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Install as LFX/GFX</source>
        <translation>安装为 LFX/GFX</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/MFX</source>
        <translation>安装为 SFX/MFX</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/EFX</source>
        <translation>安装为 SFX/EFX</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Automatically adjust install mode if problems with current mode are detected</source>
        <translation>如果检测到当前模式存在问题，则自动调整安装模式</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Pre-mix:</source>
        <translation>预混合：</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>Auto:</source>
        <translation>自动：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Allows to change silent input buffers to audible output buffers, so that delayed audio can be played.

CAUTION: Some sound card drivers can produce unwanted sounds when this is enabled.</source>
        <translation>允许将静音输入缓冲区更改为可听输出缓冲区，以便播放延迟音频。

注意：启用此功能时，某些声卡驱动程序可能会产生不必要的声音。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allow silent buffer modification</source>
        <translation>允许静默缓冲区修改</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Plays what Send: lines in other endpoints&apos; configurations hand to this endpoint, even when no program plays here. EqualizerAPOHost keeps a silent stream open on it from logon, and silent buffer modification is turned on with it.</source>
        <translation>即使没有程序在此端点播放音频，也会播放其他端点的配置中通过 Send: 行发送到此端点的音频。EqualizerAPOHost 从登录时起就在此端点上保持一个静音流。启用此选项也会允许修改静默缓冲区。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>发送</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Remove the buffer</source>
        <translation>移除缓冲区</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Wait time:</source>
        <translation>等待时间：</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+10"/>
        <source>Sets how long to wait for processing once the buffer is removed. The longer the wait, the less sound without the EQ, but the more latency.</source>
        <translation>设置移除缓冲区后等待处理的时长。等待越久，未应用 EQ 的声音越少，但延迟也随之增加。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Up to a quarter of the buffer</source>
        <translation>最多缓冲区的四分之一</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to half of the buffer</source>
        <translation>最多缓冲区的一半</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to three quarters of the buffer</source>
        <translation>最多缓冲区的四分之三</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Removing the buffer reduces latency. But when processing occasionally does not finish in time, sound without the EQ applied may come out for a moment. Applies to both input and output.</source>
        <translation>移除缓冲区可降低延迟。但如果处理偶尔未能及时完成，可能会瞬间输出未应用 EQ 的声音。对输入和输出都生效。</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Start the engine host automatically at boot</source>
        <translation>开机时自动启动引擎主机</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Unchecked, the first application that opens the driver starts the host, and it leaves a minute after the last one closes it.</source>
        <translation>关闭时，由第一个打开驱动的应用程序启动主机；最后一个应用程序关闭驱动一分钟后主机退出。</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>32-bit host support</source>
        <translation>32 位主机支持</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Also registers the driver entry where 32-bit applications look for it. Unavailable when this build ships no 32-bit wrapper.</source>
        <translation>同时在 32 位应用程序查找的位置注册驱动条目。此版本不附带 32 位包装器时不可用。</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Copy Device command to clipboard</source>
        <translation>复制设备命令到剪贴板</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Please select the devices for which Equalizer APO is to be installed:</source>
        <translation>请选择要安装 Equalizer APO 的设备：</translation>
    </message>
    <message>
        <location line="-340"/>
        <source>Please select exactly one activated device by clicking on its name</source>
        <translation>请点击名称选择一个已激活的设备</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialog</name>
    <message>
        <source>Playback devices</source>
        <translation type="vanished">播放设备</translation>
    </message>
    <message>
        <source>Capture devices</source>
        <translation type="vanished">捕获设备</translation>
    </message>
    <message>
        <source>Error while accessing the registry</source>
        <translation type="vanished">访问注册表时出错</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is disabled</source>
        <translation type="vanished">由于设备已被禁用，无法测试 APO 安装</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is unplugged</source>
        <translation type="vanished">由于设备未连接，无法测试 APO 安装</translation>
    </message>
    <message>
        <source>Error</source>
        <translation type="vanished">错误</translation>
    </message>
    <message>
        <source>Equalizer APO works but original APO could not be initialized. Maybe unset &quot;Use original APO&quot; in troubleshooting options.</source>
        <translation type="vanished">均衡器 APO 可以工作，但原始 APO 无法初始化。也许可以在故障排除选项中取消设置“使用原始 APO”。</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialogClass</name>
    <message>
        <source>Testing APO installation on devices...</source>
        <translation type="vanished">在设备上测试 APO 安装...</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">连接器</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">设备</translation>
    </message>
    <message>
        <source>Pre-mix</source>
        <translation type="vanished">预混合</translation>
    </message>
    <message>
        <source>Post-mix</source>
        <translation type="vanished">混合后</translation>
    </message>
</context>
<context>
    <name>DeviceTestThread</name>
    <message>
        <location filename="../DeviceTestThread.cpp" line="+82"/>
        <source>Could not initialize COM for device testing.</source>
        <translation>无法为设备测试初始化 COM。</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+2"/>
        <source>COM initialization failed (0x%1).</source>
        <translation>COM 初始化失败 (0x%1)。</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+185"/>
        <source>Restarting audio service...</source>
        <translation>正在重新启动音频服务...</translation>
    </message>
    <message>
        <location line="-180"/>
        <location line="+185"/>
        <source>Restart failed.</source>
        <translation>重启失败。</translation>
    </message>
    <message>
        <location line="-170"/>
        <source>Checking APO installation...</source>
        <translation>检查 APO 安装...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Could not prepare the device test.</source>
        <translation>无法准备设备测试。</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+50"/>
        <source>Could not remove the device test registration: %1</source>
        <translation>无法移除设备测试注册信息：%1</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Received unknown device GUID %1.</source>
        <translation>收到未知的设备 GUID %1。</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <location filename="../DeviceTestView.cpp" line="+493"/>
        <source>Check failed for %n device(s).</source>
        <translation>
            <numerusform>%n 个设备检查失败。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Setting install mode for %1 %2 to %3.</source>
        <translation>将 %1 %2 的安装模式设置为 %3。</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Trying other configurations...</source>
        <translation>尝试其他配置...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Checks done. No problems were detected.</source>
        <translation>检查完成。未发现任何问题。</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location filename="../DeviceTestView.cpp" line="+1"/>
        <source>Checks done. Problems were detected for %n device(s).</source>
        <translation>
            <numerusform>检查已完成。检测到 %n 个设备存在问题。</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>DeviceTestView</name>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="+113"/>
        <location line="+196"/>
        <location filename="../DeviceTestView.cpp" line="-47"/>
        <source>Checking</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-183"/>
        <source>Details are in %1.</source>
        <translation type="unfinished">详情请查看 %1。</translation>
    </message>
    <message>
        <location line="+160"/>
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
        <location filename="../DeviceTestView.cpp" line="+57"/>
        <source>Windows audio</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+42"/>
        <location filename="../DeviceTestView.cpp" line="-8"/>
        <source>Not checked</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="-420"/>
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
        <location line="+1"/>
        <source>Close</source>
        <translation type="unfinished">关闭</translation>
    </message>
    <message>
        <location line="+77"/>
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
        <location line="+1"/>
        <source>The cause could not be determined. Update the device&apos;s driver and check again; some drivers load no APO at all.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
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
        <location line="+1"/>
        <source>Close programs that manage audio devices, then check again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not changed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change to this device. The device was left as it was before.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close programs that manage audio devices, then try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Partly changed</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change, and undoing it did not finish either. The device may be left partly changed.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restart the computer before trying again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not checked: the device is disabled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The device is disabled in Windows, so nothing could be played through it.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enable the device, then open the Device Selector and press OK to check again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not checked: the device is unplugged</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing is plugged into this device, so nothing could be played through it.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Plug it in, then open the Device Selector and press OK to check again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not checked: the device could not be opened</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing could be played through this device, so the APO on it could not answer.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+16"/>
        <location line="+4"/>
        <location line="+4"/>
        <source>Open the Device Selector and press OK to check again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="-21"/>
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
        <location line="+1"/>
        <source>Restart the computer, then open the Device Selector and press OK.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Windows blocked the setting the check uses to hear from the APO, so the check did not run.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close security programs that block changes to system settings, then open the Device Selector and press OK.</source>
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
        <location line="+83"/>
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
