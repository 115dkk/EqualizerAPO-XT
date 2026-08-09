# Developer documentation
This is the developer documentation for **EqualizerAPO-XT**. It is for people who want to review, build or experiment with the source, or write their own APO. If you only want to use the software, see the [user documentation](Documentation).

*Disclaimer: this documentation is written to the best of our knowledge. It is neither complete nor guaranteed to be free of errors.*
## Building EqualizerAPO-XT
XT is a Windows project. The C++ projects target the Visual Studio 2022 / 2026 toolset with the Windows 10 SDK; the GUI tools are built with Qt. The original Equalizer APO build expected libraries installed under `C:\Program Files`; XT instead keeps every dependency under a local `deps/` folder and provides a setup script that fetches them.

### Prerequisites
* **Visual Studio 2022 Build Tools** (or 2026), including the components `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` and `Microsoft.VisualStudio.Component.VC.ATL`. ATL is required — without it the `EqualizerAPO.dll` link step cannot find `atls.lib`. The XT projects build with the VS 2026 toolset `v145`; on a VS 2022 machine pass `/p:PlatformToolset=v143`.
* **Python 3** — used to install Qt through `aqtinstall`.
* **Qt 6.10.1 (MSVC 2022 x64)** — installed under `Qt/6.10.1/msvc2022_64/`. Needed by the Configuration Editor, the Device Selector and the Update Checker.
* **External libraries** — **not** committed to the repository: [FFTW](https://www.fftw.org/), [libsndfile](https://libsndfile.github.io/libsndfile/), [muParserX](https://github.com/beltoforion/muparserx), [TCLAP](http://tclap.sourceforge.net/), and the MIT-licensed Steinberg VST3 *pluginterfaces*. They are placed under `deps/` at build time.
* **Velopack** (`vpk` CLI, `dotnet tool install -g vpk`) — used only by CI to package the binaries into the `Setup.exe` / `.nupkg` artifacts. Local development does not need it.

The script **`setup-build.ps1`** downloads the binary dependencies (per SIMD variant), clones the header-only libraries (TCLAP and the VST3 pluginterfaces), and installs Qt. See [setup-build.ps1](https://github.com/115dkk/EqualizerAPO-XT/blob/main/setup-build.ps1) and the in-repo `docs/LocalDependencySetup.md` for the exact layout and the MSBuild / qmake commands.

### Source code organization
The solution `EqualizerAPO.sln` groups these projects:

* **Common** — a static library with the filter engine (`FilterEngine`, `FilterConfiguration`, `IFilter`), the individual filter implementations under `filters/`, the muParserX extensions under `parser/`, and shared modules grouped under `audio/`, `dsp/`, `platform/`, `runtime/`, `services/`, `text/`, and `vst/`. The convolution code lives in `libHybridConv-0.1.1/`. The other projects link against it.
* **EqualizerAPO** — the Audio Processing Object DLL (`EqualizerAPO.dll`). It contains the COM boilerplate, implements the APO interfaces, and calls into the Common filter engine. Being ATL-based, it needs `atls.lib`.
* **Editor** — the Qt-based Configuration Editor. `Editor.exe` is the Velopack package's main executable and handles every Velopack install/update/uninstall hook through `services/install/ApoRegistration` and `services/update/VelopackBootstrap`.
* **DeviceSelector** — the Qt utility shown after the first install so the user can pick the audio devices to register the APO for. It replaces the original Configurator.
* **UpdateChecker** — the Qt tool that runs at logon and notifies the user when a newer release for the build channel is available.
* **Benchmark** — a console program for testing the audio processing without installing it on a device. Handy for experimenting with filter types and measuring performance.
* **VoicemeeterClient** — a helper for Voicemeeter integration.
* **Tests** — the unit and regression test projects: `HybridConvTests`, `EditorLogicTests` and `AudioRegressionTests`.
* **Setup** — the sample configuration files (`config/`) and the documentation `.url` shortcuts that the CI packaging step bundles into the Velopack output.

CI lives in `.github/workflows/build.yml`: it builds the x64 `sse2`, `avx`, `avx2`, `avx512` and `avx10_1` variants plus ARM64 `neon`, then calls `vpk pack` to produce the per-channel installers (see [the installation section](Documentation#installation) and the in-repo `docs/SimdBuildMatrix.md`). Pull requests build AVX2 only; a push to `main` builds every variant and creates a GitHub Release. The original NSIS installer has been removed.

## APO development
An APO (Audio Processing Object) is a user-space module loaded by the Windows Audio Service to process sample data before it reaches the device driver. APOs are normally shipped with the audio driver and signed so they cannot bypass audio DRM. There are two kinds: **GFX** (global effect, applied after the streams are mixed) and **LFX** (local effect, applied before mixing). An output device can have one GFX and one LFX APO; an input device can have one LFX APO. An APO is a COM object identified by a GUID under which it is registered. Microsoft's documents [Custom Audio Effects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-vista-custom-audio-effects) and [Reusing System Effects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/reusing-windows-vista-system-effects) describe the model and provide samples.

Adding a custom APO means clearing two obstacles: the audio engine must be allowed to load unsigned APOs, and the device's existing APO must be chained to the custom one so it keeps working. The registry changes that make the Windows Audio Service load the DLL are:

1. Under `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio`, set the DWORD **`DisableProtectedAudioDG`** to `1`. This turns off the APO signature check so unsigned APOs load. Applications that need a protected audio path may then change behaviour or refuse to output audio.
1. Register the APO COM class under `HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\<GUID>`. Set a class name as the key's default value, create the `InprocServer32` subkey with the DLL path as its default value, and set `ThreadingModel` appropriately.
1. Create `HKEY_LOCAL_MACHINE\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\<GUID>`. This is normally done by the DDK function `RegisterAPO` (declared in `audioenginebaseapo.h`); `UnregisterAPO` removes it.
1. Register the APO for a device under `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\<endpoint GUID>\FxProperties` (the `Render` path is output devices; `Capture` is input devices). In `FxProperties`, the value `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1` holds an LFX APO's GUID and `...,2` a GFX APO's. These usually already point at the driver's APOs, so registering the custom APO means replacing one value and saving the original elsewhere (Equalizer APO stores it under `HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO\Child APOs`) — both to restore it on uninstall and to load and call the original APO so it keeps working. Since Windows 8.1 the values ending in `,5` (LFX) and `,6` (GFX) are also used, and when an APO is registered through them, one registered through the old `,1`/`,2` values is ignored. Also since 8.1 the values `{d3993a3f-99c2-4402-b5ec-a92a0367664b},5` and `,6` specify processing modes (type MULTI_SZ); both normally need to be `{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}`, the default processing mode.

In XT this registration and the matching cleanup are performed by `services/install/ApoRegistration`, driven from the Velopack hooks the Editor handles.

## See also
* [Documentation](Documentation) — using EqualizerAPO-XT.
* [Configuration reference](Configuration-reference) — the configuration command set.
* [이 문서의 한국어판 (Korean)](Korean-Developer-documentation)
