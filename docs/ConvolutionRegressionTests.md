# Convolution Regression Tests

This project now has a small framework-free regression test executable at
`Tests/HybridConvTests`.

## Covered Cases

- `HConvSingle` keeps sparse impulse response taps alive past 48000 samples.
- The same sparse response still works after the mix buffer position has wrapped.
- `ConvolutionFilter` recovers when the first process call uses a shorter frame
  count than the configured maximum. This reproduced the 1000 ms tail loss:
  before the fix, a first 128-frame call made the later 480-frame stream miss
  the impulse response tap at sample 48000.

## Local Run

Build the common library, build the test executable, then run it:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe' Common.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /m
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe' Tests\HybridConvTests\HybridConvTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /m
& .\Tests\HybridConvTests\x64\Release\HybridConvTests.exe
```

Expected output:

```text
HybridConvTests passed
```

The test project copies `libfftw3.dll` and `sndfile.dll` from the configured
dependency folders into its output directory.

## CI

GitHub Actions builds `Tests\HybridConvTests\HybridConvTests.vcxproj` for each
SIMD matrix entry and runs the executable after the non-Qt projects finish.
