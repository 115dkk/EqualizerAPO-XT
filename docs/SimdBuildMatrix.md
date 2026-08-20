# SIMD Build Matrix

EqualizerAPO-XT produces separate binaries for each SIMD target instead of
runtime-dispatching one universal x64 binary.

## CI Variants

| Matrix name | Platform | SIMD variant | MSBuild instruction set | Qt flag | Dependency release assets | Update channel | Installed name |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `windows-x64-sse2` | `x64` | `sse2` | `NotSet` | none | vcpkg `fftw3[sse2,threads]`, vcpkg `libsndfile`, rebuilt `muparserx` | `x64-sse2` | EQ APO XT |
| `windows-x64-avx` | `x64` | `avx` | `AdvancedVectorExtensions` | `/arch:AVX` | vcpkg `fftw3[avx,threads]`, vcpkg `libsndfile`, rebuilt `muparserx` | `x64-avx` | EQ APO XT AVX |
| `windows-x64-avx2` | `x64` | `avx2` | `AdvancedVectorExtensions2` | `/arch:AVX2` | `*-x64-avx2` | `x64-avx2` | EQ APO XT AVX2 |
| `windows-x64-avx512` | `x64` | `avx512` | `AdvancedVectorExtensions512` | `/arch:AVX512` | `*-x64-avx512` | `x64-avx512` | EQ APO XT AVX-512 |
| `windows-x64-avx10_1` | `x64` | `avx10_1` | `AdvancedVectorExtensions101` | `/arch:AVX10.1` | `*-x64-avx10` | `x64-avx10-1` | EQ APO XT AVX10 |
| `windows-arm64` | `ARM64` | `neon` | none | none | `*-arm64` | `arm64-neon` | EQ APO XT Neon |

The per-variant facts above are defined once in `.github/simd-variants.psd1`.
Each variant ships as a Velopack package whose `packId` is
`EqualizerAPO-XT-<channel>`. Velopack appends `-<channel>-Setup.exe`, so the
release Setup asset is named `EqualizerAPO-XT-<channel>-<channel>-Setup.exe`
(for example `EqualizerAPO-XT-arm64-neon-arm64-neon-Setup.exe`). The
channel-less `EqualizerAPO-XT-Setup.exe` on the same release is the
auto-detect installer described in `docs/AutoDetectInstaller.md`.

The "Installed name" column (`Title` in the manifest) is what the user sees
after installation: the Start menu/desktop shortcut, the Apps & Features
entry and the per-channel Setup window. It is display metadata only - the
`packId`, the update channel and the install directory keep the
`EqualizerAPO-XT-<channel>` identity. Renaming a Title is safe for existing
installs: Velopack's update apply rewrites the uninstall registry entry and
renames existing shortcuts (it finds them by target path, not by name), so
installed apps pick up a new Title on their next update.

## Runtime Compatibility

- x64 installers are variant-specific. Users need a CPU and Windows build that
  can execute the selected variant.
- `x64-sse2` uses MSVC's default x64 code generation and does not pass an AVX
  `/arch` flag to project or Qt builds. It is the compatibility channel for
  older x64 CPUs.
- `x64-avx` is for CPUs with AVX but no AVX2.
- ARM64 builds do not pass x64 AVX flags and use the ARM64 dependency artifacts.
- `x64-sse2` and `x64-avx` build lower-SIMD third-party binaries in CI instead
  of reusing the AVX2 dependency DLLs.

## SIMD implementation (Highway portability layer)

The DSP kernels in `Common` (`libHybridConv_eapo.cpp`, `BiQuadFilter.cpp`,
`PreampFilter.cpp` and the float↔double boundary in `FilterEngine.Process.cpp`)
are written once with [Google Highway](https://github.com/google/highway) and
compiled in **static per-target dispatch** mode: each variant TU is built with
its `/arch` flag and Highway resolves `HWY_NAMESPACE` to the matching target.
There is no `HWY_DYNAMIC_DISPATCH` / runtime target table — the per-variant
binary split above *is* the dispatch. Highway is a header-only build dependency
(`deps\highway`, `HIGHWAY_INCLUDE`), fetched in CI like the VST3 pluginterfaces.

| Variant | `/arch` | Highway target | double lanes |
| --- | --- | --- | --- |
| `x64-sse2` | NotSet | `HWY_SSE2` | 2 |
| `x64-avx` | `/arch:AVX` | 128-bit | 2 |
| `x64-avx2` | `/arch:AVX2` | `HWY_AVX2` | 4 |
| `x64-avx512` | `/arch:AVX512` | `HWY_AVX3` | 8 |
| `x64-avx10_1` | `/arch:AVX10.1` | `HWY_AVX3` (shares AVX-512 codegen) | 8 |
| `arm64` | none | `HWY_NEON` | 2 |

The ARM64 build now runs real NEON kernels; before the Highway port every SIMD
block was guarded out on ARM64 and the audio path fell back to scalar.

## Test Policy

`EditorLogicTests` runs for every matrix entry because it does not execute SIMD
audio kernels.

`HybridConvTests` is built for every matrix entry. CI runs it for SSE2, AVX,
AVX2, and ARM64. GitHub-hosted x64 runners do not guarantee AVX-512 or AVX10.1
at runtime; executing those test binaries on an incompatible runner would fail
with an illegal instruction before the test can report a useful result.

`AudioRegressionTests` compares each runnable variant's FilterEngine output to
committed references within −120 dBFS, and `cross_variant_compare.py` cross-checks
the SSE2/AVX/AVX2 outputs. The `convolution_short` case gates the convolution hot
path specifically, so the Highway kernels (including the ARM64 NEON path) are held
to the pre-port intrinsic output.
