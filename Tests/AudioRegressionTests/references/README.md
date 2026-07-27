# Audio regression references

## These baselines are deliberately not regenerated

The harness used to hand each case's whole signal to `process()` in one call,
which meant a stored baseline could not observe anything that a stateful filter
carries between calls. Every case now drives the engine in fixed-size blocks the
way Windows drives the APO, with `blockFrames` chosen per case to divide the
signal length exactly.

The baselines below were **not** regenerated for that change, and that is the
point: they were produced by whole-buffer runs, the blocked runs reproduce them
to within floating-point noise (worst case `3.1e-17` on `convolution_short`,
`1.0e-38` on `biquad_peaking_1khz`, exact elsewhere), and the tolerance is
-120 dB. So the files now assert something stronger than they used to. They pin
the output *and* the fact that chunking the signal differently does not change
it.

Keep it that way. If a future change genuinely requires new baselines, prefer
regenerating from a whole-buffer run and letting the blocked comparison prove
invariance again, rather than regenerating in block mode - the latter would
silently discard the evidence.

Note that `blockFrames` must divide `frames`. `ConvolutionFilter` freezes its
block length at `initialize()` and mutes any block of a different size, so a
short final block would silence the tail instead of testing it. The harness
fails the case rather than letting that happen quietly.

## Provenance

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

## Processing-filter coverage

Three more cases extend coverage to the remaining processing filters. They are
generated the same way as the baseline set (run `AudioRegressionTests.exe ...
--generate-references`, which writes `<case>.raw` into this directory). The
reference files are `iir_order2_lowpass.raw`, `channel_left_only.raw`, and
`loudnesscorrection_bypassed.raw`.

* `iir_order2_lowpass` (`configs/iir_order2_lowpass.txt`): exercises
  `IIRFilterFactory` / `IIRFilter`. The config is
  `Filter 1: ON IIR Order 2 Coefficients 0.25 0.5 0.25 1 0 0`, a 3-tap
  symmetric smoothing response (`b = [0.25, 0.5, 0.25]`, `a = [1, 0, 0]`). With
  a stereo impulse input the first three output samples per channel reproduce
  the `b` coefficients, so the reference is human-checkable. Input
  `ImpulseStereo`, 48000 Hz, 2 channels, 256 frames.

* `channel_left_only` (`configs/channel_left_only.txt`): exercises
  `ChannelFilterFactory` / `ChannelFilter`. The config scopes a following
  `Preamp: -6 dB` to the left channel only (`Channel: L`), so with a stereo DC
  input the left channel is attenuated to 10^(-6/20) ≈ 0.5012 while the right
  channel stays at 1.0. Input `DCStereo`, 48000 Hz, 2 channels, 256 frames.

* `loudnesscorrection_bypassed` (`configs/loudnesscorrection_bypassed.txt`):
  exercises `LoudnessCorrectionFilterFactory` and its `FilterParameters`
  parsing. The config is `LoudnessCorrection: State 0 ReferenceLevel -20
  ReferenceOffset 0`. `State 0` makes the filter a deterministic pass-through
  (output equals input). `State 1` is intentionally avoided here: that path
  reads the live system master volume through `VolumeController` and runs a
  background parameter-update thread, so its output is not stable enough for a
  stored baseline across machines/CI runners. Input `ImpulseStereo`, 48000 Hz,
  2 channels, 256 frames.

## All-pass coverage

Three cases pin the 2nd-order all-pass before the reform in issue #228 changes
anything above the DSP. An all-pass is flat, so a magnitude comparison would
pass no matter what happened to it; only the impulse response can see the
filter at all, because the whole filter is in where the energy lands in time.

The reference files are `allpass_q0707.raw`, `allpass_q10.raw`, and
`allpass_bw1oct.raw`. All three use `ImpulseStereo`, 48000 Hz, 2 channels, 8192
frames.

* `allpass_q0707` (`Filter 1: ON AP Fc 1000 Hz Q 0.707`): the width the reform
  makes the default for newly created filters.

* `allpass_q10` (`Filter 1: ON AP Fc 1000 Hz Q 10`): the width the current
  Editor template creates, and therefore the width existing user configs carry.
  The reform must not migrate it, so this baseline is what proves it did not.

* `allpass_bw1oct` (`Filter 1: ON AP Fc 80 Hz BW Oct 1`): the case the Editor
  silently rewrites to `Q 1` today, because the all-pass width-mode selector
  offers no second entry to restore. The engine has always honoured `BW Oct`
  here; this baseline is the engine's answer, so after the Editor fix an
  open-and-save round trip has to reproduce it byte for byte.

### First-order sections

Four more cases cover the 1st-order all-pass the `Order 1` parameter reaches.
`allpass_order1_fc100` and `allpass_order1_fc1000` pin the section itself.

`allpass_order1_x2` and `allpass_order2_q05` are the identity that anchors the
whole derivation: two 1st-order sections at one frequency in series are exactly
one 2nd-order section at Q 0.5. They are separate configurations
(`Filter 1: ON AP Fc 500 Hz Order 1` twice, against
`Filter 1: ON AP Fc 500 Hz Q 0.5 Order 2`) whose baselines came out
**byte-identical to each other**, SHA-256
`943DAF819D9B499A159395274FF28B4D65AF9C928C6B81940C2D797AD92F44DD`. If a future
change to the 1st-order coefficients breaks that equality, the two files stop
matching each other as well as their own baselines. `HybridConvTests` proves the
same identity on the coefficients.

Unlike the older baselines these were generated in whole-buffer mode
(`blockFrames` temporarily set equal to `frames`) and are verified by the
committed 512-frame blocked run, which reproduces them exactly
(`maxAbsError = 0`). That is the order the top of this file asks for, and it
means these three files pin the output *and* the block-size invariance, the
same way the older ones do.

## Hilbert and Velvet coverage

Three references were introduced together with the two built-in spatial
commands, which had no earlier implementation to preserve:

* `hilbert_roles.raw` runs
  `Hilbert: Shift=L Align=R Direction=-90` on a stereo impulse. The left
  channel pins the normalized 1025-tap quadrature FIR and the right channel
  pins the matching 512-sample alignment delay. It uses 4096 frames in
  512-frame blocks.
* `velvet_static.raw` runs the default Static kernel on a stereo impulse. It
  pins deterministic per-channel sparse taps, normalization, mixing and
  channel independence over 4096 frames in 256-frame blocks.
* `velvet_dynamic.raw` runs a 100 ms Evolution interval and a 50 ms Transition
  over stereo DC for 12288 frames in 256-frame blocks. The signal crosses two
  renewal boundaries, so the stored output covers bank generation and the
  equal-power transition rather than only initial setup.

These references were generated in an isolated reference directory and only
the three new files were copied here; none of the earlier baselines were
regenerated. `HilbertVelvetTests` separately proves FIR antisymmetry, phase
sign, 0 dB mid-band normalization, unit-energy/correlation bounds, smooth
dynamic renewal and host-block invariance.

## VST (not covered)

The VST processing filter is intentionally not given a regression case. It
requires an external VST plugin that is not part of the repository, and the
audit calls for skipping it on CI. Add a case here only once a redistributable,
deterministic test plugin is available.
