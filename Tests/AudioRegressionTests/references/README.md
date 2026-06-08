# Audio regression references

These raw float baselines were generated from commit `329db3a`
(`fix(engine): link all filter factories from Common.lib and cache FFTW wisdom`).

That commit is after the 1000 ms convolution-tail fix and after the
AudioRegressionTests harness was able to exercise real filter factories, but
before the later DSP hot-path optimization in `309e9e8`.

Generation command shape:

```powershell
AudioRegressionTests.exe `
  --variant baseline-329db3a `
  --config-dir Tests\AudioRegressionTests\configs `
  --ref-dir Tests\AudioRegressionTests\references `
  --out-dir <scratch-output> `
  --generate-references
```

## `convolution_short`

`convolution_short.raw` was added later, for the Highway SIMD port, to give the
convolution hot path (`libHybridConv_eapo.cpp`) regression coverage it did not
have before. It was generated from the pre-Highway (hand-written intrinsic)
`avx2` build so the Highway port is gated against the exact prior output.

The case convolves a stereo impulse with `configs\ir_short.wav`, a mono / 48000 Hz
/ 32-bit float / 64-tap impulse response whose only non-zero taps are
`h[0]=1.0, h[20]=0.5, h[40]=0.25`. With an impulse input the (normalized)
convolution output reproduces the IR, so the reference is human-checkable and the
FFT round-trip plus complex multiply-accumulate are all exercised. Regenerate the
fixture with:

```python
import struct
SR, N = 48000, 64
s = [0.0] * N
s[0], s[20], s[40] = 1.0, 0.5, 0.25
data = b"".join(struct.pack("<f", v) for v in s)
fmt = struct.pack("<HHIIHH", 3, 1, SR, SR * 4, 4, 32)  # IEEE float, mono, 32-bit
body = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
open(r"configs\ir_short.wav", "wb").write(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)
```
