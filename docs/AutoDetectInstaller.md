# Auto-detect installer

`EqualizerAPO-XT-Setup.exe` is a small front-door installer. It detects the
machine's CPU architecture and the best supported x86 instruction set, then
downloads and runs the matching per-variant Velopack `Setup.exe`. The user picks
nothing; the right build installs itself.

This document records why the feature is shaped the way it is and what it does
*not* touch, so the working release pipeline stays understandable.

## The problem

EqualizerAPO-XT ships six build variants, one per instruction set, each as its
own Velopack channel:

| Channel       | For |
| ------------- | --- |
| `x64-sse2`    | baseline x64 (no AVX) |
| `x64-avx`     | x64 with AVX, no AVX2 |
| `x64-avx2`    | most modern x64 |
| `x64-avx512`  | x64 with AVX-512 |
| `x64-avx10-1` | x64 with AVX10.1 |
| `arm64-neon`  | Windows on ARM64 |

Before this feature the release page offered six `…-Setup.exe` files and the
user had to know their own CPU's AVX level to choose. Most people cannot, so
they either guessed (and lost performance, or installed a build their CPU can't
run) or picked the AVX2 default regardless.

## Why this approach

Two alternatives were considered and rejected:

- **Runtime dispatch (one fat binary).** A single build that picks SIMD kernels
  at runtime. This means abandoning the per-variant builds and rebuilding the
  FFTW bundle, the CI matrix, the Velopack channels and the release scripts —
  a large change for the same end result the user sees.
- **Velopack-native detection.** Velopack has no built-in "detect the CPU and
  install the matching build" step. The exact feature request,
  [velopack/velopack#7](https://github.com/velopack/velopack/issues/7), is
  closed as *not planned*. Velopack channel switching (`ExplicitChannel` +
  `AllowVersionDowngrade`) only works **within one `packId`**, but each variant
  here is published under its own `packId` (`EqualizerAPO-XT-<channel>`), so a
  channel switch cannot move between them without re-identifying the app and
  breaking update continuity for existing installs.

The front-door detector delivers exactly what the user wanted — one download,
the correct variant, silently — while changing **nothing** about how the six
variants are packaged or how each one updates itself.

## What it does

1. **Detect the native architecture.** `IsWow64Process2` reports the *native*
   machine even when the detector runs under emulation, so a single x86 binary
   correctly sees ARM64. ARM64 → `arm64-neon`.
2. **Detect the best x86 instruction set** (x64 machines only) with `CPUID` plus
   an `XGETBV`/`XCR0` check that the OS actually enabled the wider register
   state. Highest match wins:

   | Detected | Channel |
   | --- | --- |
   | AVX10.1 (leaf `0x24` version ≥ 1, 512-bit, OS ZMM state) | `x64-avx10-1` |
   | AVX-512F (leaf 7.0 EBX[16], OS ZMM state) | `x64-avx512` |
   | AVX2 (leaf 7.0 EBX[5], OS YMM state) | `x64-avx2` |
   | AVX (leaf 1 ECX[28], OS YMM state) | `x64-avx` |
   | none of the above | `x64-sse2` |

3. **Download the matching installer** from the latest release using GitHub's
   `…/releases/latest/download/<asset>` redirect, so the detector never needs to
   be rebuilt per release and always installs the newest build. The per-variant
   asset name is `EqualizerAPO-XT-<channel>-<channel>-Setup.exe` (the channel
   appears twice because each variant's `packId` already contains the channel).
4. **Verify the download** against the release's `SHA256SUMS.txt` asset before
   anything is executed. See
   [Download integrity verification](#download-integrity-verification).
5. **Run the verified `Setup.exe`.** By default Velopack shows its normal
   install UI for the auto-selected variant. Passing `--silent` to the detector
   forwards `-s/--silent` to that `Setup.exe` for fully unattended installs.

The detector is built as a 32-bit (x86) Win32 GUI app. x86 runs natively on x64
and under emulation on ARM64 (both Windows 10 and 11 ARM), so one binary covers
every target the app supports. `CPUID`/`XGETBV` report true CPU and OS state
regardless of process bitness, so detection from x86 is accurate.

### What it deliberately does not change

- The six per-channel Velopack packages (`vpk pack --packId … --channel …`) are
  untouched.
- Each installed variant keeps updating itself within its own channel via the
  build-time `EAPO_UPDATE_CHANNEL` define and the background updater in
  `helpers/VelopackBootstrap`.
- The per-variant `…-Setup.exe` files remain on the release page for users who
  want to pick a specific build by hand.

## Download integrity verification

CI publishes a `SHA256SUMS.txt` asset to every release. It contains one line
per asset in the standard `sha256sum` format — `<lowercase-hex-sha256>` then
two spaces then `<filename>` — and covers at least every `…-Setup.exe` asset.

After downloading the per-variant installer, the detector

1. downloads `SHA256SUMS.txt` through the same
   `…/releases/latest/download/<asset>` redirect,
2. finds the line whose file name matches the downloaded asset (the name
   comparison ignores ASCII case, and the `sha256sum -b` binary-mode form
   `<hash> *<name>` is accepted too), and
3. computes the file's SHA-256 with Windows CNG (`bcrypt.dll`) and compares it
   to the listed hash.

If the checksums file cannot be downloaded, does not list the asset, or the
hash does not match, the downloaded installer is deleted, an error pointing at
the releases page is shown, and nothing is executed.

Exit codes in normal (non-`--detect-only`) mode:

| Code | Meaning |
| --- | --- |
| 0 | success (with `--silent`, the per-variant `Setup.exe` exit code is forwarded instead) |
| 2 | the installer download failed |
| 3 | the verified installer could not be started |
| 4 | integrity verification failed |

Caveat for scripts: with `--silent`, a successful launch forwards the
per-variant `Setup.exe`'s own exit code, so a nonzero code can also originate
from Velopack rather than from the rows above. Treat 0 as success and any
nonzero code as failure instead of branching on specific values.

Both files are fetched via the `latest` redirect, so a release published
between the two downloads can cause a one-off mismatch; running the installer
again resolves it. `SHA256SUMS.txt` is the last asset CI uploads, so during
the final minute of a release publish (or after a half-failed release job) the
checksums file can be missing and the installer fails with code 4 until the
release job finishes or is re-run. The check protects the integrity of the download path. It is
not a substitute for code signing, because the checksums file comes from the
same release as the installers (see Limitations).

## ARM64 update-channel convergence

While building this, an existing mismatch surfaced: ARM64 builds baked
`EAPO_UPDATE_CHANNEL=arm64` (build.yml) and `simd-variants.psd1` declared the
channel as `arm64`, but releases are published under `arm64-neon`. The ARM64
auto-updater therefore looked for `releases.arm64.json`, which does not exist,
so ARM64 never self-updated.

Everything now converges on the already-published `arm64-neon`: the baked
channel (build.yml), the manifest `Channel`, and the release-notes channel
table. No migration is needed — older ARM64 installs were already unable to
update, and new ones now resolve the correct feed.

## Channel list is duplicated — keep it in sync

The detector hard-codes the six channel strings (it is compiled C++ and cannot
read `simd-variants.psd1`). If a variant is added, renamed or removed, update
`Installer/AutoInstaller.cpp` alongside `.github/simd-variants.psd1`,
`.github/workflows/build.yml` and `.github/scripts/New-ReleaseNotes.ps1`. The
release-notes script already fails the build if the manifest channels drift from
its own table, which is the loudest of these guards.

## Build and CI

- `Installer/Installer.vcxproj` builds the detector for `Win32` only. It has no
  external dependencies (just `winhttp`, `bcrypt`, `comctl32`, `shell32`), so it
  builds without FFTW/Qt/etc.
- The decision logic (channel mapping, asset grammar, checksum parsing) lives
  in `Installer/AutoInstallerLogic.{h,cpp}`, which EditorLogicTests compiles
  directly, so PRs verify it without building the installer.
- The avx2 leg of `Build-Solution.ps1` builds the whole binary on every PR; a
  compile break no longer waits for release day.
- The `create-release` job builds it (`/p:Platform=Win32 /p:Configuration=Release`)
  and uploads it to the release tag as `EqualizerAPO-XT-Setup.exe`, before the
  release notes step so the notes can feature it as the recommended download.

## Local verification

```
Installer\Release\EqualizerAPO-XT-Setup.exe --detect-only
```

`--detect-only` prints the resolved channel and the download URL and exits
without downloading or installing anything. Its process exit code is the channel
index (`0`=sse2, `1`=avx, `2`=avx2, `3`=avx512, `4`=avx10-1, `5`=arm64-neon), so
the detection can be checked from a script.

## Limitations

- The detector and the downloaded `Setup.exe` are unsigned, like the existing
  per-channel installers, so SmartScreen will warn on first run. The SHA-256
  verification catches corrupted or substituted downloads, but it does not
  prove publisher identity the way a code signature would.
- It requires network access at install time. On failure it shows the releases
  page URL so the user can pick a build manually.
