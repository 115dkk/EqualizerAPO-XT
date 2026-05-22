# SIMD Build Matrix

EqualizerAPO-XT produces separate binaries for each SIMD target instead of
runtime-dispatching one universal x64 binary.

## CI Variants

| Matrix name | Platform | SIMD variant | MSBuild instruction set | Qt flag | Dependency release assets | Installer artifact |
| --- | --- | --- | --- | --- | --- | --- |
| `windows-x64-avx2` | `x64` | `avx2` | `AdvancedVectorExtensions2` | `/arch:AVX2` | `*-x64-avx2` | `EqualizerAPO_Setup-x64-avx2` |
| `windows-x64-avx512` | `x64` | `avx512` | `AdvancedVectorExtensions512` | `/arch:AVX512` | `*-x64-avx512` | `EqualizerAPO_Setup-x64-avx512` |
| `windows-x64-avx10_1` | `x64` | `avx10_1` | `AdvancedVectorExtensions101` | `/arch:AVX10.1` | `*-x64-avx10` | `EqualizerAPO_Setup-x64-avx10_1` |
| `windows-arm64` | `ARM64` | `neon` | none | none | `*-arm64` | `EqualizerAPO_Setup-arm64` |

## Runtime Compatibility

- x64 installers are variant-specific. Users need a CPU and Windows build that
  can execute the selected variant.
- ARM64 builds do not pass x64 AVX flags and use the ARM64 dependency artifacts.
- Non-AVX x64 systems are not covered by the current release matrix. Add a
  scalar x64 dependency set and installer variant before claiming support for
  those machines.

## Test Policy

`EditorLogicTests` runs for every matrix entry because it does not execute SIMD
audio kernels.

`HybridConvTests` is built for every matrix entry, but CI only runs it for AVX2
and ARM64. GitHub-hosted x64 runners do not guarantee AVX-512 or AVX10.1 at
runtime; executing those test binaries on an incompatible runner would fail with
an illegal instruction before the test can report a useful result.
