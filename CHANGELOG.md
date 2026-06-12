# Changelog

**English** | [한국어](CHANGELOG.ko.md)

All notable changes to EqualizerAPO-XT since it was forked from TheFireKahuna's
equalizerAPO64 tree (last upstream commit `7156020`, 2025-12-16). Work on this
fork started on 2026-05-22.

Versions are bumped automatically by CI from Conventional Commits message
types, so some version numbers were skipped (1.7, 1.9, 1.12.1, 1.14, and 1.16
were never released). Tags up to v1.10.1 carried a `-main.<run>` suffix; from v1.11.0 on,
tags are clean `vX.Y.Z` names. Installers for every version are on the
[Releases page](https://github.com/115dkk/EqualizerAPO-XT/releases).

## Unreleased

- The Editor draws its own window chrome: the native Windows caption is
  replaced by a skinnable title bar (dragging, snapping, edge resizing and
  double-click maximize stay native), and the menu bar plus every dropdown
  menu now follow each skin's design language — lit glass with luminous
  separators (Studio Glass), a terminal title line with icon-free mono menus
  (Precision Minimal), a calm rounded header and menu cards (Soft Lab), an
  engraved brushed panel with LED checks (Hardware Rack), and a grid masthead
  with cell menus (Signal Matrix). The Edit menu's remaining 2005-era icons
  were replaced with modern stroke icons. A "Native title bar" toggle in the
  Interface menu restores the stock caption after a restart. The offscreen
  gallery now captures title bar, menu bar and an open menu per skin with
  Korean sample text, guarding against the Hangul clipping reported from the
  field. ([#88])

## v1.24.0 — 2026-06-12

- The main toolbar lost its stock Windows look and 2005-era icons. A new
  `ISkin::styleMainToolbar` hook dresses it per skin: the top edge of the
  glass with light pooling under unboxed buttons (Studio Glass), a terminal
  command line with NEW/OPEN/SAVE as mono text commands (Precision Minimal),
  a calm header band with pastel tiles and a real stadium toggle (Soft Lab),
  a brushed master rail with transport buttons, screws and an LCD save-state
  well (Hardware Rack), and a board header of square function cells with a
  status lamp (Signal Matrix). The save-state badge is now styled by each
  skin (the old hardcoded pill is gone), and the offscreen gallery captures
  every toolbar. ([#85])

## v1.22.0 — 2026-06-12

- The "add filter" picker is no longer one flat list of every template: it is
  a compact dropdown anchored at the add button, and each skin presents the
  catalog in its own design language — a frosted-glass panel (Studio Glass),
  a numbered terminal index with digit-jump (Precision Minimal), a rounded
  settings menu (Soft Lab), a 1U module preset browser with an LCD search
  strip (Hardware Rack), and a two-axis crosspoint instrument (Signal
  Matrix). Skins contribute their picker through the new
  `ISkin::createFilterPicker` hook; the offscreen gallery captures every
  picker. ([#81])

## v1.21.0 — 2026-06-12

- Gain knobs (the Preamp card knob and the biquad gain dial) now turn across
  a configurable ±range, default ±20 dB, set via View > Interface > Knob gain
  range. Typed values still accept each command's full range and simply peg
  the knob; the previous fixed ±100 dB preamp span made small turns jump by
  tens of dB. ([#78])
- The analysis panel starts at a more modest height, docks at the bottom by
  default like the original Equalizer APO, and its position is picked from a
  Pos dropdown (Top / Bottom / Right) in the panel's control bar instead of
  the implicit Ctrl+Alt+G cycling. ([#78])
- A deliberately emptied Copy command can be refilled from the GUI in every
  skin. The crosspoint grid (Signal Matrix) and patch-bay (Hardware Rack)
  always offer the full device channel surface; the step list (Precision
  Minimal) and equation blocks (Soft Lab) seed a row per device channel and
  gained a per-row [+] menu for adding sources, and clearing a factor removes
  that source. Seeded empty rows write nothing to the config line. ([#78])

## v1.20.0 — 2026-06-12

- The Editor now writes a crash minidump and a small text report (version,
  exception address, the last skin switched to) to
  `%LOCALAPPDATA%\EqualizerAPO-XT\crashdumps` whenever it dies unexpectedly,
  instead of disappearing without a trace. This was added to hunt a
  machine-specific crash when selecting certain skins ([#75]); CI now also
  keeps debug symbols for every released binary so those dumps can be
  analyzed. ([#76])

## v1.19.0 — 2026-06-12

- The five Editor skins are now fully differentiated visual identities
  instead of palette variations. Each answers command-type marking, hover,
  disabled state, Include/VST presentation, corner language and hierarchy
  with its own shapes, textures and typography — glass cards with glowing
  arc knobs (studio), a zero-radius hairline terminal (minimal), a roomy
  rounded settings look (soft), skeuomorphic rack hardware with painted
  screws, nameplates and pointer knobs (rack), and a grid instrument panel
  with LED-ring encoders and crosspoint hover (matrix). Built by five
  isolated implementation agents and integrated after a differentiation
  review; the full record is in docs/skin-integration-report.md. ([#73])

## v1.18.0 — 2026-06-12

- Channel rows on the modern cards now edit their selection in place with
  checkable chips (device channels, ALL, custom/virtual names, plus a field
  to add new names) instead of requiring the raw-text editor or the legacy
  dialog flow. Equivalent selections serialize byte-identically to what the
  old dialog wrote. ([#70])
- Dropdowns no longer render undersized: all skins share a readable sizing
  floor, the toolbar dropdowns follow the system font size, and popup lists
  widen to their longest entry instead of eliding it. ([#70])
- Skin program Phase 0 plumbing landed for the five-skin overhaul
  (issues #66–#68): knob painting and command-row chrome are now delegated
  through `ISkin` hooks with appearance-preserving defaults (proven
  pixel-identical), and a headless screenshot gallery
  (`Editor --skin-gallery`) renders every skin's representative rows; CI
  uploads the images as the `skin-gallery` artifact. ([#70])

## v1.17.2 — 2026-06-12

- All accumulated cppcheck findings were triaged. Real defects fixed: a
  resource leak on the throwing paths of the file-access check used by the
  Include GUI, ARM64-native VST libraries being misreported as having the
  wrong architecture, and a `log10(0)` call in Benchmark for silent output.
  The rest of the tree received mechanical hardening (default member
  initializers, const-reference passing, explicit `wstring::npos` checks),
  and CI now runs a pinned cppcheck 2.21.0 as a blocking gate at a zero
  baseline. ([#64])
- The remaining filter config grammars (Stage, Include, Device,
  If/ElseIf/Else/EndIf, Eval and inline backtick expressions, IIR,
  LoudnessCorrection) moved into shared command codecs used by both the
  engine and the Editor, completing the migration started in #57. Each codec
  has round-trip tests, and the unused ParameterArchive helper was
  removed. ([#63])
- The `Channel:` and `Convolution:` config grammars moved into shared command
  codecs used by both the engine and the Editor, with round-trip tests. This
  fixed the Channel GUI ignoring comma-separated selectors. The
  `IFilterFactory` consumption contract and the intentional Editor/UpdateChecker
  update-path separation are now documented in the code. ([#57])
- The biweekly audit now runs on a Windows runner with Git Bash and a
  pre-provisioned buildable tree, so audits can compile and execute the test
  suites instead of reading the code blind. ([#58])
- `main` pushes that do not bump the version cannot produce a new release, so
  they now skip the build matrix entirely; a full-matrix build stays available
  via `workflow_dispatch`. ([#61])
- README refreshed to the current project state, this changelog added, and
  Korean translations of both provided. ([#60], [#62])

## v1.17.1 — 2026-06-11

First round of fixes from biweekly audit issue #53.

- The auto-detect installer now verifies downloaded setups against a
  `SHA256SUMS.txt` release asset before launching them; CI publishes the
  checksum file with every release. ([#56])
- libHybridConv buffer ownership moved from unguarded process-global maps into
  the convolution structs themselves, removing a latent data race between APO
  instances. Audio output is bit-identical. ([#56])
- New `EngineOrchestrationTests` suite covers channel-name routing, `Copy`
  semantics, and config-swap crossfading through the public engine API. ([#56])
- The CI build matrix is generated from the `.github/simd-variants.psd1`
  manifest, binary dependency downloads are tag-pinned and SHA-256-verified,
  and a lint step fails CI when installer channel names drift from the
  manifest. Pull requests now genuinely build only the primary avx2 variant
  (the old PR filter silently built all six). ([#55])
- The biweekly audit runs on Claude Fable 5. ([#54])

## v1.17.0 — 2026-06-10

- **Auto-detect installer**: a new front-door `EqualizerAPO-XT-Setup.exe`
  detects the CPU (architecture and AVX level) and downloads the matching
  build, so users no longer pick a SIMD variant by hand. The ARM64 update
  channel was aligned with the published `arm64-neon` name. ([#52])
- Second round of fixes from audit issue #48: a shared test harness with new
  regression/helper/parser coverage, a runtime VST2 host test built around a
  self-built test plug-in, the `VSTPluginInstance` god-file split into cohesive
  units, one owning parse routine for BiQuad config lines, a config-file codec
  extracted from MainWindow, shared parse/serialize codecs for the Preamp,
  Delay, GraphicEQ, Copy, and VSTPlugin GUIs, the SIMD variant set defined once
  in `simd-variants.psd1`, and qmake failing loudly when no SIMD flag is
  passed. ([#51])

## v1.15.3 — 2026-06-09

First round of fixes from audit issue #48. ([#50])

- The convolution IR cache is size-bounded and `ConvolutionFilter` resource
  handling was hardened.
- Filter factories register through a central registry, allocations go through
  a typed checked allocator, and the engine warns when a recognized command
  produces no filter. VST plug-in initialization failures are logged with
  reasons.
- The Editor's known-command list is derived from the factory registry instead
  of a hand-maintained copy, and the legacy filter-list UI path is frozen and
  documented. Release-notes SIMD channel tables are driven from one table.

## v1.15.2 — 2026-06-09

- Audit workflow actions updated for Node 24. ([#49])

## v1.15.1 — 2026-06-08

- Added a biweekly automated code-audit workflow that runs Claude against the
  codebase and files its findings as a GitHub issue. ([#46], [#47])
- The B-plan epic for runtime SIMD dispatch is recorded in
  `docs/RuntimeDispatchEpic.md`. ([#45])

## v1.15.0 — 2026-06-08

- **Portable SIMD with Google Highway**: the four hand-written SIMD sites
  (convolution kernels, BiQuadFilter, PreampFilter, float↔double conversion)
  were ported from per-ISA intrinsics to single Highway kernels compiled per
  variant. ARM64 moves from scalar fallbacks to real NEON. A new
  `convolution_short` regression case with a committed reference gates the
  port; output stays within the regression tolerance on all variants.
  ([#43], [#44])
- CI fails the build when a Qt application executable is missing instead of
  packaging an incomplete release. ([#44])

## v1.13.1 — 2026-06-08

- VST plug-in library load/unload is thread-safe. ([#42])
- User documentation was rewritten in English and Korean and is published to
  the GitHub Wiki by CI. ([#36], [#37], [#38], [#39])
- Audio regression reference data is committed, and cross-variant comparison
  fails on missing outputs instead of passing silently. ([#40], [#41])

## v1.13.0 — 2026-06-07

- **Native VST3 hosting** through the Steinberg VST3 SDK pluginterfaces, with
  double-precision processing where the plug-in supports it. ([#35])

## v1.12.x — 2026-06-03 ~ 2026-06-06

- Skin and theme switching no longer re-polishes the whole widget tree, making
  it near-instant. (v1.12.5, [#34])
- Modern filter card icons render correctly and Copy gain labels no longer
  overlap. (v1.12.4, [#33])
- The filter knob is a true rotary control that tracks the cursor, and release
  pipelines are serialized with a CI concurrency group. (v1.12.3, [#32])
- CI moved to the `windows-2025-vs2026` runner image with per-runner platform
  toolsets, GitHub Actions moved off deprecated Node.js 20, and
  `velopack_libc.dll` ships under the name the app actually loads — fixing
  startup of packaged builds. (v1.12.2, [#29], [#30], [#31])
- Legacy filter cards use the modern AudioKnob, channel selection badges are
  colour-coded, and Qt high-DPI scaling is enabled so the UI is not tiny on 4K
  displays. (v1.12.0, [#28])

## v1.11.0 — 2026-06-03

- **Automatic updates**: the Editor downloads new releases in the background
  through the Velopack SDK and applies them when the Editor exits. ([#27])
- Font weights render correctly and the Copy routing renderer swaps when the
  skin changes. ([#27])

## v1.10.x — 2026-06-02

- The APO reports its effect through `IAudioSystemEffects2::GetEffectsList`,
  so Windows can show what processing is active. (v1.10.0, [#25])
- Copy routing editors are seeded with the device channel list instead of an
  empty canvas. (v1.10.1, [#26])

## v1.8.0 — 2026-05-31 ~ 2026-06-02

- **Per-skin Copy routing renderers**: each of the five Editor skins renders
  channel routing with its own visual language, driven by a new `ISkin`
  delegation engine. DM Sans, DM Mono, and Pretendard fonts are embedded and
  QSS fonts are tokenized. ([#22])
- A flaky Editor startup crash was fixed by serializing FFTW planner access.
  ([#22])
- Non-realtime COM/Win32 resources are wrapped in RAII, and the APO registers
  itself in-process instead of shelling out to regsvr32. ([#23])

## v1.6.0 — 2026-05-26 ~ 2026-05-27

- The APO passes audio through unchanged when the sample format is not
  IEEE_FLOAT 32/64 instead of corrupting it, and the Editor surfaces the
  passthrough status so users can see when EQ is inactive. ([#21])
- Three Modern Card rendering bugs found by variant diagnostics were fixed,
  and the Modern Card right header toolbar is visible again. ([#19], [#20])

## v1.5.x — 2026-05-24 ~ 2026-05-25

- **Five distinct Editor skins** (studio, minimal, soft, rack, matrix) with
  per-skin token QSS, plus automated version bumping from Conventional
  Commits. (v1.5.0, [#11], [#12])
- All filter factories link from Common.lib (some filters silently did
  nothing before) and FFTW wisdom is cached. (v1.5.1, [#13])
- Performance passes over the DSP hot path, Editor analysis panel, and
  convolution: decoded impulse responses are cached per filter type, only the
  toggled row refreshes on enable/disable, and the Velopack update check is
  deferred 60 s from startup. (v1.5.1, [#14], [#15], [#16], [#17])
- Commented-out rows no longer grey the whole card and BiQuad cards show a
  richer summary. (v1.5.2, [#18])

## v1.4.3 — 2026-05-22 ~ 2026-05-24

- **Velopack migration (phases 1–5)**: headless APO registration, Velopack
  install/update/uninstall hooks in the Editor, raw binaries packed directly
  into Velopack, a runtime helper that triggers background updates, and the
  NSIS installer with its scheduled-task update path removed. ([#4])
- **AudioRegressionTests**: a regression suite that renders DSP scenarios and
  compares output against committed references, run in CI with cross-variant
  comparison and cppcheck; PR and push builds were split. ([#6])
- SSE2 and AVX release channels were added (threaded FFTW in lower SIMD
  builds), Velopack feed assets for AVX are recognized, legacy card editors
  were replaced, and default Qt styling was swept out of the Editor UI
  ([#3]).
- A stage-level profiler measures the audio pipeline in Benchmark. ([#5])
- An import-to-config flow scans a referenced config's dependencies and copies
  them into the config directory from the Include card. ([#7])
- The install directory grants LOCAL SERVICE access so audiodg can load the
  APO, with diagnostics and recovery scripts under `tools/`. ([#9], [#10])
- `setup-build.ps1` provisions a local build environment. ([#8])

## v1.4.2 — 2026-05-22

Fork bootstrap on top of TheFireKahuna's tree.

- **Convolution tail fix**: reverb tails no longer cut out around the 1000 ms
  mark after frame size changes, guarded by new hybrid-convolution regression
  tests. ([#2])
- Broad modernization refactor: RAII for filter configuration storage and COM
  objects, `nullptr`/typed casts/typed buffer copies, large implementation
  files split by responsibility, filter factories registered outside
  FilterEngine, and FilterEngine synchronization modernized. ([#1], [#2])
- Convolution file path handling was tightened and path parsing expanded.
  ([#2])
- A Velopack release workflow and update feed integration were added ([#1],
  [#2]). GitHub Actions builds were stabilized: dependencies download from
  release assets, Qt installs directly in CI, actions run on Node 24, and
  ARM64 builds use a native MSVC environment.
- First version of the modern card-based Editor UI.

[#1]: https://github.com/115dkk/EqualizerAPO-XT/pull/1
[#2]: https://github.com/115dkk/EqualizerAPO-XT/pull/2
[#3]: https://github.com/115dkk/EqualizerAPO-XT/pull/3
[#4]: https://github.com/115dkk/EqualizerAPO-XT/pull/4
[#5]: https://github.com/115dkk/EqualizerAPO-XT/pull/5
[#6]: https://github.com/115dkk/EqualizerAPO-XT/pull/6
[#7]: https://github.com/115dkk/EqualizerAPO-XT/pull/7
[#8]: https://github.com/115dkk/EqualizerAPO-XT/pull/8
[#9]: https://github.com/115dkk/EqualizerAPO-XT/pull/9
[#10]: https://github.com/115dkk/EqualizerAPO-XT/pull/10
[#11]: https://github.com/115dkk/EqualizerAPO-XT/pull/11
[#12]: https://github.com/115dkk/EqualizerAPO-XT/pull/12
[#13]: https://github.com/115dkk/EqualizerAPO-XT/pull/13
[#14]: https://github.com/115dkk/EqualizerAPO-XT/pull/14
[#15]: https://github.com/115dkk/EqualizerAPO-XT/pull/15
[#16]: https://github.com/115dkk/EqualizerAPO-XT/pull/16
[#17]: https://github.com/115dkk/EqualizerAPO-XT/pull/17
[#18]: https://github.com/115dkk/EqualizerAPO-XT/pull/18
[#19]: https://github.com/115dkk/EqualizerAPO-XT/pull/19
[#20]: https://github.com/115dkk/EqualizerAPO-XT/pull/20
[#21]: https://github.com/115dkk/EqualizerAPO-XT/pull/21
[#22]: https://github.com/115dkk/EqualizerAPO-XT/pull/22
[#23]: https://github.com/115dkk/EqualizerAPO-XT/pull/23
[#25]: https://github.com/115dkk/EqualizerAPO-XT/pull/25
[#26]: https://github.com/115dkk/EqualizerAPO-XT/pull/26
[#27]: https://github.com/115dkk/EqualizerAPO-XT/pull/27
[#28]: https://github.com/115dkk/EqualizerAPO-XT/pull/28
[#29]: https://github.com/115dkk/EqualizerAPO-XT/pull/29
[#30]: https://github.com/115dkk/EqualizerAPO-XT/pull/30
[#31]: https://github.com/115dkk/EqualizerAPO-XT/pull/31
[#32]: https://github.com/115dkk/EqualizerAPO-XT/pull/32
[#33]: https://github.com/115dkk/EqualizerAPO-XT/pull/33
[#34]: https://github.com/115dkk/EqualizerAPO-XT/pull/34
[#35]: https://github.com/115dkk/EqualizerAPO-XT/pull/35
[#36]: https://github.com/115dkk/EqualizerAPO-XT/pull/36
[#37]: https://github.com/115dkk/EqualizerAPO-XT/pull/37
[#38]: https://github.com/115dkk/EqualizerAPO-XT/pull/38
[#39]: https://github.com/115dkk/EqualizerAPO-XT/pull/39
[#40]: https://github.com/115dkk/EqualizerAPO-XT/pull/40
[#41]: https://github.com/115dkk/EqualizerAPO-XT/pull/41
[#42]: https://github.com/115dkk/EqualizerAPO-XT/pull/42
[#43]: https://github.com/115dkk/EqualizerAPO-XT/pull/43
[#44]: https://github.com/115dkk/EqualizerAPO-XT/pull/44
[#45]: https://github.com/115dkk/EqualizerAPO-XT/pull/45
[#46]: https://github.com/115dkk/EqualizerAPO-XT/pull/46
[#47]: https://github.com/115dkk/EqualizerAPO-XT/pull/47
[#49]: https://github.com/115dkk/EqualizerAPO-XT/pull/49
[#50]: https://github.com/115dkk/EqualizerAPO-XT/pull/50
[#51]: https://github.com/115dkk/EqualizerAPO-XT/pull/51
[#52]: https://github.com/115dkk/EqualizerAPO-XT/pull/52
[#54]: https://github.com/115dkk/EqualizerAPO-XT/pull/54
[#55]: https://github.com/115dkk/EqualizerAPO-XT/pull/55
[#56]: https://github.com/115dkk/EqualizerAPO-XT/pull/56
[#57]: https://github.com/115dkk/EqualizerAPO-XT/pull/57
[#58]: https://github.com/115dkk/EqualizerAPO-XT/pull/58
[#60]: https://github.com/115dkk/EqualizerAPO-XT/pull/60
[#61]: https://github.com/115dkk/EqualizerAPO-XT/pull/61
[#62]: https://github.com/115dkk/EqualizerAPO-XT/pull/62
[#63]: https://github.com/115dkk/EqualizerAPO-XT/pull/63
[#64]: https://github.com/115dkk/EqualizerAPO-XT/pull/64
[#70]: https://github.com/115dkk/EqualizerAPO-XT/pull/70
[#73]: https://github.com/115dkk/EqualizerAPO-XT/pull/73
[#75]: https://github.com/115dkk/EqualizerAPO-XT/issues/75
[#76]: https://github.com/115dkk/EqualizerAPO-XT/pull/76
[#78]: https://github.com/115dkk/EqualizerAPO-XT/pull/78
[#81]: https://github.com/115dkk/EqualizerAPO-XT/pull/81
[#85]: https://github.com/115dkk/EqualizerAPO-XT/pull/85
[#88]: https://github.com/115dkk/EqualizerAPO-XT/pull/88
