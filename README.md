# EqualizerAPO-XT

**English** | [한국어](README.ko.md)

EqualizerAPO-XT is an active fork of [Equalizer APO 1.4.2](https://sourceforge.net/p/equalizerapo/) for Windows. It keeps Equalizer APO's system-wide audio processing model while modernizing the audio engine, build pipeline, and GUI tools.

This fork builds on earlier double-precision work from [equalizer-apo-64](https://github.com/chebum/equalizer-apo-64) and later SIMD/build work from TheFireKahuna's Equalizer APO forks.

Looking to configure it? The [GitHub Wiki](https://github.com/115dkk/EqualizerAPO-XT/wiki) has installation help and a full [configuration reference](https://github.com/115dkk/EqualizerAPO-XT/wiki/Configuration-reference) for every filter command, in English and Korean.

## Project Status

The original fork goals are complete: the convolution tail bug is fixed, the engine code was refactored and put under regression tests, the Editor UI was rebuilt, SIMD support was consolidated, and releases ship as Velopack packages with automatic updates. The full history since the fork is in [CHANGELOG.md](CHANGELOG.md).

Current work areas:

1. Runtime SIMD dispatch in a single binary, replacing the per-variant release channels ([docs/RuntimeDispatchEpic.md](docs/RuntimeDispatchEpic.md)).
2. Acting on findings from the biweekly automated code audit.
3. Refining the Editor skins from community feedback rounds (round 1: the Soft pastel rework, dark-mode state contrast, and the compact analysis panel; round 3: the modern GraphicEQ card, the insertion contract and the skinned Device Selector, [#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172)).
4. Phase and time. The analysis graph now switches between magnitude, phase and
   group delay, the all-pass filter has its own card and a 1st-order section,
   and `Delay` and the all-pass share a "Phase & Time" group in the picker. An
   all-pass changes nothing about level, so a magnitude-only graph could never
   show one ([#228](https://github.com/115dkk/EqualizerAPO-XT/issues/228),
   [docs/features/phase-and-time.md](docs/features/phase-and-time.md)).
5. Editors for the programmatic config commands (`If:`/`ElseIf:`/`Else:`/`EndIf:`/`Eval:`) — complete. Each skin presents blocks with its own instrument driven by the analysis run, the picker inserts the vocabulary, custom-coefficient IIR lines have their own card, and lines with inline `` `expression` `` parameters keep their card in a dynamic mode ([#178](https://github.com/115dkk/EqualizerAPO-XT/pull/178), [#182](https://github.com/115dkk/EqualizerAPO-XT/pull/182), [#183](https://github.com/115dkk/EqualizerAPO-XT/pull/183), [#184](https://github.com/115dkk/EqualizerAPO-XT/pull/184)).

## Features

- Double-precision internal audio processing for complex filter chains.
- Convolution, GraphicEQ, parametric EQ, VST2/VST3, and standard Equalizer APO filter support.
- MultiConvolution filter for true-stereo and BRIR (Binaural Room Impulse Response) playback: `MultiConvolution: L=0+1 R=2+3 brir.wav` convolves each channel's own signal with its mapped channels of one multichannel impulse response and sums them back, independent of the Channel command - the fan-out/sum pattern the in-place Convolution filter cannot express, in one line. Each mapped channel takes an optional factor with Copy's grammar (`L=0.5*0+1`; `-1` inverts the phase, `-6dB` works too). The Editor edits the mapping in every skin's routing view.
- A built-in 1025-tap linear-phase Hilbert transform with explicit phase-shift
  and latency-alignment roles. For example,
  `Hilbert: Shift=SL,SR Align=L,R Direction=-90` applies the selected ±90°
  transform to `SL,SR` and the matching 512-sample delay to `L,R`.
- A built-in sparse velvet-noise decorrelator. `Velvet: Mode=Dynamic` gives
  every channel an independent, unit-energy kernel and renews the kernels with
  an equal-power transition; Static mode and the amount, time spread, density,
  evolution, transition, decay, and deterministic variation are all explicit
  parameters. Frequency-response analysis freezes Dynamic mode to one labelled
  snapshot because a time-varying response has no single permanent curve.
  The independent, MIT-licensed
  [Dynamic Velvet Decorrelator VST3](https://github.com/115dkk/Velvet-Noise-Decorrelator-VST3)
  exposes the same portable DSP outside EqualizerAPO-XT.
- Native VST3 hosting through the Steinberg VST3 SDK (MIT-licensed pluginterfaces), with 64-bit (double) processing where the plug-in supports it.
- Portable SIMD kernels written once with [Google Highway](https://github.com/google/highway) and compiled per variant: SSE2, AVX, AVX2, AVX-512, and AVX10.1 on x64, NEON on ARM64.
- Modernized Qt Editor: card-based filter UI and five fully differentiated visual skins — each with its own row chrome, knob rendering, and Copy routing renderer ([docs/skin-integration-report.md](docs/skin-integration-report.md)) — plus embedded fonts and high-DPI scaling.
- Automatic updates: the Editor downloads new releases in the background and applies them on exit. A standalone UpdateChecker tool provides notify-only checks.
- Auto-detect installer that picks the matching SIMD build for the local CPU and verifies the download against the release checksums before running it.
- AOCL-FFTW, libsndfile, muparserx, TCLAP, and Qt-based GUI tools.
- Shared VC++ runtime DLLs for better Windows compatibility.
- GitHub Actions pipeline for builds, tests, installers, and releases, plus a biweekly automated code audit that builds the tree and runs the test suites.

## Installation

Install from the [Releases page](https://github.com/115dkk/EqualizerAPO-XT/releases). A push to `main` builds all supported variants and creates a GitHub Release with Velopack-packaged installers and a source code zip.

The recommended download is **EqualizerAPO-XT-Setup.exe**, an auto-detect installer. It detects your CPU (architecture and AVX level), downloads the matching build, and verifies it against the release's `SHA256SUMS.txt` before launching it. The per-channel `…-Setup.exe` files stay available for installing a specific build. See [docs/AutoDetectInstaller.md](docs/AutoDetectInstaller.md).

After installation the Editor keeps itself current: it downloads newer releases in the background and applies them when the Editor closes. The flow is documented in [docs/VelopackUpdates.md](docs/VelopackUpdates.md).

## Documentation

User documentation lives in the [GitHub Wiki](https://github.com/115dkk/EqualizerAPO-XT/wiki) in English and Korean, synced from `Wiki/github-wiki/` in this repository. Developer notes live under [docs/](docs/).

## Building

The project uses Visual Studio, Qt, Velopack, and a small set of external libraries. Running [setup-build.ps1](setup-build.ps1) provisions all of them for a local build (binary dependencies, header-only checkouts, and Qt 6.10.1). The manual layout is documented in [docs/LocalDependencySetup.md](docs/LocalDependencySetup.md).

The forked dependency repositories are:

- [AOCL-FFTW 5.1 / FFTW 3.3.10](https://github.com/thefirekahuna/amd-fftw)
- [muparserx 4.0.13](https://github.com/thefirekahuna/muparserx)
- [libsndfile 1.2.2](https://github.com/thefirekahuna/libsndfile)
- [tclap 1.2.5](https://github.com/thefirekahuna/tclap)

Two header-only dependencies are checked out rather than vendored: [Google Highway](https://github.com/google/highway) under `deps/highway` and the Steinberg [VST3 pluginterfaces](https://github.com/steinbergmedia/vst3_pluginterfaces) under `deps/vst3sdk/pluginterfaces`.

By default, project files look for dependencies under the repo-local `deps/` directory:

- `deps/fftw`
- `deps/libsndfile`
- `deps/muparserx`
- `deps/tclap`
- `deps/highway`
- `deps/vst3sdk`

The same environment variables can override those defaults:

- `FFTW_INCLUDE`, `FFTW_LIB`
- `LIBSNDFILE_INCLUDE`, `LIBSNDFILE_LIB`
- `MUPARSERX_INCLUDE`, `MUPARSERX_LIB`
- `TCLAP_ROOT`
- `HIGHWAY_INCLUDE`
- `VST3_SDK`

The SIMD variant set is defined once in `.github/simd-variants.psd1`. That manifest drives the CI matrix, the pinned dependency downloads, the installer channel names, and the release notes. CI currently builds these variants:

- `windows-x64-sse2`
- `windows-x64-avx`
- `windows-x64-avx2`
- `windows-x64-avx512`
- `windows-x64-avx10_1`
- `windows-arm64`

Pull requests build only the primary `avx2` variant. Pushes to `main` build all six when the automatic version bump produces a new version; pushes that cannot produce a release (docs, CI, refactor-only changes) skip the build matrix. Manual `workflow_dispatch` runs always build all six. The SIMD matrix, dependency artifact names, installer artifact names, and test policy are tracked in [docs/SimdBuildMatrix.md](docs/SimdBuildMatrix.md).

Qt tools are built through qmake in CI and in the documented local setup. A full Visual Studio solution build also needs a working Qt VS Tools/QtMsBuild setup.

## Tests

`Tests/` holds five projects: `EditorLogicTests` and `HybridConvTests` (unit tests), `EngineOrchestrationTests` (engine routing and config-swap behavior), `AudioRegressionTests` (engine output compared against committed references, also run across SIMD variants in CI), and `TestVst2Plugin` (a self-built plug-in used to test the VST2 host at runtime). Test policy per variant is part of [docs/SimdBuildMatrix.md](docs/SimdBuildMatrix.md).
