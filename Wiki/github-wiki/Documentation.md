# Documentation
This is the user documentation for **EqualizerAPO-XT**. If you write your own APO or want to build the project from source, see the [Developer documentation](Developer-documentation) instead. Once you have it installed, the detailed command list lives in the [Configuration reference](Configuration-reference).
## Installation
XT publishes a small compatibility front-door installer together with six CPU-specific 64-bit packages. The APO/application payload has **no 32-bit build** — only x64 and ARM64.

1. Open the [Releases page](https://github.com/115dkk/EqualizerAPO-XT/releases) and download **`EqualizerAPO-XT-Setup.exe`**.
1. Run it while connected to the internet. It detects the native architecture and the highest AVX level that both the CPU and Windows support, downloads the matching Velopack installer, verifies it against the release's `SHA256SUMS.txt`, and only then starts it.
1. For an offline transfer, recovery, or an intentionally fixed build, the per-channel `…-Setup.exe` files remain available. The channels are `x64-sse2`, `x64-avx`, `x64-avx2`, `x64-avx512`, `x64-avx10-1` and `arm64-neon`; choose one manually only when you know the target machine supports it. The [auto-detect installer design](https://github.com/115dkk/EqualizerAPO-XT/blob/main/docs/AutoDetectInstaller.md) documents the exact selection and integrity checks.
1. The selected Velopack installer unpacks the application and registers EqualizerAPO-XT. You do not need to remember an install path; the location is recorded in the registry (see [Where things live](#where-things-live)).
1. After the first install the **Device Selector** opens. Tick the playback and/or capture devices that Equalizer APO should attach to. If you are unsure, pick your default output device — you can see which one that is under **Start → Settings → System → Sound**. You can run the Device Selector again at any time from the installation folder if you want to add or remove devices later.
1. Allow Windows to restart the audio service (or reboot). A freshly registered APO is not picked up until the audio engine restarts.
1. Once the service has restarted the APO is active. With the bundled example configuration this is only noticeable as a small drop in volume and a mild low-frequency boost. To make it do something useful, continue with the [First configuration](#first-configuration) section.

### Where things live
EqualizerAPO-XT records its paths in the registry key **`HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO`**:

* `InstallPath` — the directory the application was installed to.
* `ConfigPath` — the `config` folder that holds your configuration files. A normal XT install uses `%LOCALAPPDATA%\EqualizerAPO-XT\config`; an intentionally chosen custom path is preserved, so the registry value is authoritative.

The easiest way to reach the configuration folder is to open the **Configuration Editor**, which points at it directly. The folder ships with `config.txt` (the main file, loaded automatically) plus several ready-made examples: `example.txt`, `demo.txt`, `convolution.txt`, `iir_lowpass.txt`, `multichannel.txt` and `selective_delay.txt`.

### Automatic updates
XT installs through [Velopack](https://velopack.io/), so updates are delivered per build channel rather than by reinstalling from scratch:

* **UpdateChecker** runs at logon (via a scheduled task) and tells you when a newer release for your channel is available. It checks the GitHub release feed for your variant and respects a 24-hour throttle and any version you chose to skip.
* The **Configuration Editor** also updates itself: about a minute after launch it quietly downloads a newer build for your channel in the background, then applies it silently when you close the editor. The new version comes up the next time you start it.

## First configuration
1. Open the configuration folder (see [above](#where-things-live)). The main file is `config.txt`; Equalizer APO loads it automatically and reloads it whenever you save.
1. Open `config.txt` in the Configuration Editor or any text editor. It defines a preamplification value and then includes `example.txt`. To confirm the APO is running, start any audio playback and change the `Preamp` value while sound is playing — the volume should change the moment you save the file.
1. To build your own correction, the classic approach is to measure your system with [Room EQ Wizard](https://www.roomeqwizard.com/) (REW) and export the result as filter text. EqualizerAPO-XT also includes a graphical **Configuration Editor** where you can add and tune filters directly, which many users find easier for small adjustments.

<img src="RoomEQWizard.png" width="600"><br><em>Room EQ Wizard (the markers A–F are referenced in the steps below)</em>

A full Room EQ Wizard tutorial is outside the scope of this page, but the basic measurement-to-filters flow is:

1. Click **Measure** (mark **A**) to open the measurement dialog. Run **Check Levels** first, set a sensible output volume, then **Start Measuring**. A frequency-response graph appears when it finishes.
1. Click **EQ** (mark **B**) and choose an equalizer type (mark **C**). Use **Generic**, or **FBQ2496** if you prefer bandwidth over Q. Other equalizer types are not guaranteed to be compatible.
1. Click **EQ Filters** (mark **D**). Add filters by setting *Control* to *Manual*, *Type* to *PK/PEQ*, then adjusting *Frequency*, *Gain* and *Q*/*Bw Oct*. The graph updates live. Peaking filters are usually the right choice for room correction, though the other [filter types](Configuration-reference#filter) are available too.
1. Save the REW filter set with **Save this filter set** (mark **E**) so you can reload it later.
1. Export in the format Equalizer APO reads: from the main window open **File** (mark **F**) → **Export** → **Filter Settings as text**, and save it into the configuration folder under a new name.
1. Edit `config.txt` so its `Include` line points at your new file. The change takes effect immediately.

That is your first working configuration. For the complete syntax — filters, channel routing, delay, convolution, graphic EQ and the expression language — see the [Configuration reference](Configuration-reference). The REW [help](https://www.roomeqwizard.com/help/) explains the measurement side in much more depth.

## Convolution
One of the reasons to use the XT fork is its convolution support. The [Convolution](Configuration-reference#convolution) command loads an impulse response from a sound file (WAV, FLAC, OGG and the other formats handled by [libsndfile](https://libsndfile.github.io/libsndfile/)) and convolves the selected channels with it. XT removes the impulse-response length cap of the original and simplifies the setup; the bundled `convolution.txt` is a starting point. The impulse response's sample rate must match the audio device's sample rate, and configurations reload automatically when an impulse-response file inside the config folder changes.

## Troubleshooting
This section collects the usual reasons Equalizer APO might not behave as expected.

### Original APOs and the Device Selector
By default Equalizer APO tries to keep the effects that shipped with your sound-card driver (the "original APOs") working alongside it. On some systems this chaining causes glitches. If playback or recording becomes unstable, open the **Device Selector**, select the affected device, enable the troubleshooting options, and clear both **Use original APO** checkboxes.

<img src="UseOriginalAPO.png" width="500"><br><em>Disabling the original APOs in the Device Selector</em>

Doing this loses any effects the driver provided through its own APOs, so you can also try clearing only one box to keep part of that functionality. Some drivers disable their own options when they detect another APO; installing Equalizer APO into only the pre-mix or only the post-mix stage (via the **Install APO** checkboxes) can recover some of those driver features for the other stage.

### Audio enhancements are disabled in Control Panel
If nothing you change in the configuration file has any audible effect, APOs may be switched off for the device in the Windows sound settings. Open **Start → Settings → System → Sound**, open the device's properties, and:

* if there is an **Enhancements** tab, make sure **Disable all enhancements** is unchecked — even if you use none of the listed enhancements;
* if there is no Enhancements tab, open the **Advanced** tab and make sure **Enable audio enhancements** is checked.

<img src="EnhancementsTab.png" width="350"><br><em>"Enhancements" tab — leave "Disable all enhancements" unchecked</em>
<img src="NoEnhancementsTab.png" width="350"><br><em>"Advanced" tab — keep "Enable audio enhancements" checked</em>

### Log files
When Equalizer APO hits a critical problem it appends a line to:

```
C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log
```

Under normal operation the file does not even exist — it is only created on an error. For more detail you can enable trace output: open `regedit.exe`, go to `HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO`, and set the value **`EnableTrace`** to `true`. Lines marked `(TRACE)` are then written during normal playback or recording, which is useful for checking how your configuration files are interpreted. Set `EnableTrace` back to `false` when you are done so the log does not grow without need.

XT's user-facing diagnostics live under `%LOCALAPPDATA%\EqualizerAPO\logs`:

* `Editor.log` records Editor, install/update-hook and save failures.
* `DeviceSelector.log` records device installation, removal and repair.
* Running `Editor.exe --diagnose` writes `diagnose-<time>.txt` there (and to an attached console). It only inspects the installation and does not need administrator rights.
* Editor crash minidumps and text reports go in the `crash` subfolder.

### Exclusive mode, ASIO, low-latency and raw streams
Equalizer APO runs inside the Windows audio engine as an APO, so it processes every stream the engine mixes and none that go around it. Which kind a stream is depends on the application, not on the installation:

* **WASAPI exclusive mode** (a player's "WASAPI exclusive" output, a streaming app's "exclusive mode" switch, HQPlayer, JRiver and the like) sends the audio straight from the application to the driver. The engine never sees it, so no APO runs and nothing in the Device Selector changes that. Two ways to keep the EQ: switch the application's output to shared mode, or, if the application has an ASIO output, pick the `<your driver> (EQ APO XT)` entry ([ASIO](https://github.com/115dkk/EqualizerAPO-XT/blob/main/docs/features/asio.md)).
* **ASIO** bypasses the engine the same way. XT's wrapper entry is the way to keep the EQ on such streams.
* **Low-latency shared streams** (games, DAWs and communication apps that ask for small buffers through IAudioClient3 or AudioGraph) stay inside the engine and pass through Equalizer APO like any other; the APO just gets smaller blocks. It has been run down to 32-frame blocks. Two things to know: a convolution filter's block follows the engine period, so a long impulse response costs more CPU while such an application runs; and if sound drops out only while one of them runs, enable `EnableTrace` (see [Log files](#log-files)), look for the `LockForProcess` lines that appear when the application starts, and attach that log to a report.
* **Raw mode** streams (an application that sets `AUDCLNT_STREAMOPTIONS_RAW`) skip the stream slot (SFX) but still pass the post-mix slot: the mode effect on Windows 10 and later, the endpoint effect always. An installation with both stages ticked keeps applying the post-mix part of the configuration to them. The driver decides whether raw mode exists at all; a virtual cable, for example, has none.

### Hardware-accelerated OpenAL
Applications that use OpenAL usually pose no problem because they fall back to DirectSound, which supports APOs. Some vendors, however, ship hardware-accelerated OpenAL libraries that talk to the hardware directly and bypass APOs entirely. There is no way to add APO support to hardware-accelerated OpenAL, so the options are to switch the application to another output backend, or to force OpenAL into software mode — for example by replacing `OpenAL32.dll` with the [OpenAL Soft](https://openal-soft.org/) build, or by renaming the vendor's hardware OpenAL library (often named like `*_oal.dll`) in `C:\Windows\System32` or `C:\Windows\SysWOW64`. The latter changes the sound driver and is not officially supported.

## See also
* [Configuration reference](Configuration-reference) — every command and its syntax.
* [Developer documentation](Developer-documentation) — building XT and writing your own APO.
* [이 문서의 한국어판 (Korean)](Korean-Documentation)
