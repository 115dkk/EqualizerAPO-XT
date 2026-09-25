# Development and test environment variables

Audit #250 F063/F048: three of these gates existed only in source. This is
the one list; add new `EAPO_*` variables here in the same change that
introduces them.

These are development and CI switches. None of them are required (or
useful) for end users; the released binaries behave identically with all of
them unset.

## Editor / skin gallery

- `EAPO_SKIN_GALLERY` — set by the `--skin-gallery` run itself so reference
  cards skip the audio-service ACL probe against freshly written scratch
  files. Not meant to be set by hand.
- `EAPO_GALLERY_LEGACY` — renders the heritage (legacy rows) gallery dumps
  instead of the per-skin card gallery.
- `EAPO_SWITCH_LIMIT_MS` / `EAPO_SWITCH_WARN_MS` — override the
  skin-switch stopwatch gate in the gallery's `--skin-switch-storm`
  diagnostics (defaults live in `Editor/SkinGallery.cpp`; CI passes its own
  values in `build.yml`). Raise them when judging on a loaded machine.
- `EAPO_MOVE_LIMIT_MS` / `EAPO_MOVE_WARN_MS` — the same budget for the
  `--card-move-test` gate (a card move must not rebuild the whole list);
  `Invoke-EditorOffscreenTest.ps1` passes CI's values.
- `EAPO_ANALYSIS_LAYOUT_HOLD_MS` — keeps the Editor open that many
  milliseconds (0 to 30000) after the analysis-dock layout test, so the
  window can be captured.
- `EAPO_GALLERY_LANG` — renders the gallery with a shipped translation
  (for example `ko`) instead of the default untranslated English, to judge
  translated typography.
- `EAPO_GALLERY_VST2_PLUGIN` / `EAPO_GALLERY_VST3_PLUGIN` /
  `EAPO_GALLERY_VST3_UPMIXER` — the test plug-ins the gallery's VST cards
  load (`TestVst2Plugin`, `TestVst3Plugin`, and a copy of the latter named
  `Upmixer.vst3`); `Invoke-EditorOffscreenTest.ps1` sets them.

## Editor field switches

- `EAPO_DISABLE_PANEL_FEED` — turns the VST panel's live preview off
  entirely (no meters, no monitor), the Editor as it was before the feed.
- `EAPO_DISABLE_PANEL_MONITOR` — keeps the panel's meters but never plays
  the audio a plug-in generates on its own.

Both are kill switches for the field and the control arms of the
`vst3-preview-probe` A/B checks; unset, the Editor behaves as shipped.

## ASIO

- `EAPO_WASAPI_FORCE_BRIDGE` — an integer from 2 to 8: makes the WASAPI
  exclusive target serve that many ASIO periods per device event from the
  start, instead of deciding from the first events, to exercise that path
  on a driver that does not need it.

## Tests

- `EAPO_XT_BRIR_DIR` — points `EngineOrchestrationTests` at a directory
  holding real BRIR captures (`Thead400FL.wav`, ...) to run
  `testRealBrirCrossfeed`. No CI workflow sets it, so that test is a local,
  data-in-hand check; without the variable the test states that it was
  skipped and why.
- `EAPO_XT_TEST_IR_DIR` / `EAPO_XT_TEST_IMPORT_IR` — impulse-response
  fixture locations for the convolution regression suites (see
  `docs/ConvolutionRegressionTests.md`).
- `EAPO_TEST_VST_METADATA` — makes `TestVst2Plugin` report the synthetic
  metadata variant named by the value, so `HybridConvTests` can exercise
  host-side metadata handling.

## Build / release

- `EAPO_UPDATE_CHANNEL` — compile-time define (not an environment variable
  at runtime): the Velopack channel name baked into each SIMD variant's
  binaries by the build.
- `EAPO_REPO_URL` / `EAPO_REPO_SLUG` — compile-time defines from
  `version.h` naming the canonical GitHub repository.
