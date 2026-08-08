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
