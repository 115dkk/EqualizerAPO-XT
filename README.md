# EqualizerAPO-XT

EqualizerAPO-XT is an active fork of [Equalizer APO 1.4.2](https://sourceforge.net/p/equalizerapo/) for Windows. It keeps Equalizer APO's system-wide audio processing model while modernizing the audio engine, build pipeline, and GUI tools.

This fork builds on earlier double-precision work from [equalizer-apo-64](https://github.com/chebum/equalizer-apo-64) and later SIMD/build work from TheFireKahuna's Equalizer APO forks.

## Current Focus

The first goal is to fix a reverb playback bug where the tail can disappear around the 1000 ms mark.

After that, the project will focus on:

1. Updating the Editor and helper tools so the UI is easier to use.
2. Expanding convolution support and exposing the new options in the UI.
3. Consolidating x64 SIMD support around AVX10 where it makes sense.
4. Keeping supported paths for machines that do not support AVX.

## Features

- Double-precision internal audio processing for complex filter chains.
- Convolution, GraphicEQ, parametric EQ, VST2/VST3, and standard Equalizer APO filter support.
- Native VST3 hosting through the Steinberg VST3 SDK (MIT-licensed pluginterfaces), with 64-bit (double) processing where the plug-in supports it.
- x64 SIMD builds for AVX2, AVX-512, and AVX10.1 in CI.
- ARM64 build support with native dependency builds.
- AOCL-FFTW, libsndfile, muparserx, TCLAP, and Qt-based GUI tools.
- Shared VC++ runtime DLLs for better Windows compatibility.
- GitHub Actions build pipeline for dependencies, binaries, and installers.

## Installation

Use the [Releases page](https://github.com/115dkk/EqualizerAPO-XT/releases) when XT builds are published. A push to `main` builds all supported variants and creates a GitHub Release with Velopack-packaged installers and a source code zip.

The installed update checker reads the matching Velopack channel feed and opens the correct channel setup asset when a newer release is available. The flow is documented in [docs/VelopackUpdates.md](docs/VelopackUpdates.md).

Until then, build locally or use upstream Equalizer APO for normal production use.

## Building

The project uses Visual Studio, Qt, NSIS, and a small set of external libraries.

The forked dependency repositories are:

- [AOCL-FFTW 5.1 / FFTW 3.3.10](https://github.com/thefirekahuna/amd-fftw)
- [muparserx 4.0.13](https://github.com/thefirekahuna/muparserx)
- [libsndfile 1.2.2](https://github.com/thefirekahuna/libsndfile)
- [tclap 1.2.5](https://github.com/thefirekahuna/tclap)

Local dependency setup is documented in [docs/LocalDependencySetup.md](docs/LocalDependencySetup.md).

By default, project files look for dependencies under the repo-local `deps/` directory:

- `deps/fftw`
- `deps/libsndfile`
- `deps/muparserx`
- `deps/tclap`

The same environment variables can override those defaults:

- `FFTW_INCLUDE`, `FFTW_LIB`
- `LIBSNDFILE_INCLUDE`, `LIBSNDFILE_LIB`
- `MUPARSERX_INCLUDE`, `MUPARSERX_LIB`
- `TCLAP_ROOT`

CI currently builds these variants:

- `windows-x64-avx2`
- `windows-x64-avx512`
- `windows-x64-avx10_1`
- `windows-arm64`

The SIMD matrix, dependency artifact names, installer artifact names, and test
policy are tracked in [docs/SimdBuildMatrix.md](docs/SimdBuildMatrix.md).

The broad preparation/refactoring pass is summarized in [docs/RefactoringBaseline.md](docs/RefactoringBaseline.md).

Qt tools are built through qmake in CI and in the documented local setup. A full Visual Studio solution build also needs a working Qt VS Tools/QtMsBuild setup.
