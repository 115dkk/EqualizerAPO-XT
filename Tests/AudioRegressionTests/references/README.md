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
