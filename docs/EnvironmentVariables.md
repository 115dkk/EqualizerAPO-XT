# Development and test environment variables

Audit #250 F063/F048: three of these gates existed only in source. This is
the one list; add new `EAPO_*` variables here in the same change that
introduces them.

These are development and CI switches, test fixtures and field troubleshooting
switches. None is required for normal use; unset, the released binaries use
their normal behavior.

`.github/scripts/Test-SourceSync.ps1` checks this list in both directions. It
recognises literal environment reads in C++ (including `%NAME%` config-path
fixtures), PowerShell environment-variable reads in root scripts and under
`.github/scripts`, and the CodeQL workflow read listed below. It does not trace
variable names assembled at runtime. Compile-time defines are not environment
variables and do not belong in this list.

## Editor / skin gallery

- `EAPO_GALLERY_LEGACY` — renders the heritage (legacy rows) gallery dumps
  instead of the per-skin card gallery.
- `EAPO_GATE_LOG` sends `Logging` output from a headless gate or gallery to
  the named UTF-8 file. When it is unset, empty, or cannot be opened, that
  output goes to `NUL`; Qt messages continue to use stderr.
- `EAPO_SWITCH_LIMIT_MS` / `EAPO_SWITCH_WARN_MS` - override the
  skin-switch stopwatch gate in `--skin-switch-test` (defaults live in
  `Editor/gallery/GalleryGates.cpp`; `.github/scripts/Invoke-EditorOffscreenTest.ps1`
  passes CI's values). Raise them when judging on a loaded machine.
- `EAPO_MOVE_LIMIT_MS` / `EAPO_MOVE_WARN_MS` — the same budget for the
  `--card-move-test` gate (a card move must not rebuild the whole list);
  `Invoke-EditorOffscreenTest.ps1` passes CI's values.
- `EAPO_PLUGIN_LOAD_GAP_LIMIT_MS` — the longest pause of the UI thread's
  20 ms timer the `--plugin-load-test` gate accepts while a slow fixture
  plug-in loads (default 2000); `Invoke-EditorOffscreenTest.ps1` passes CI's
  value.
- `EAPO_STALL_SAMPLE_MS` — when a timed operation of the skin-switch or
  card-move gate runs longer than this many milliseconds, the gate samples
  the GUI thread's call stack until it ends and prints a `StallWatch:` report
  (process, system and memory counters, the commonest stacks, the other
  threads at the moment the threshold was crossed, and the CPU time other
  processes used from then on). Without it the gate starts sampling at
  its warning budget (`EAPO_SWITCH_WARN_MS`, `EAPO_MOVE_WARN_MS`) or at a
  multiple of the median of the operations it has timed so far (1.5 for
  skin switches, 2 for card moves), whichever is lower.
- `EAPO_STALL_SELFTEST` — `1` makes the first two watched operations stall on
  purpose (a 900 ms sleep, then a 600 ms busy loop), to check that the
  `StallWatch:` report names them. Never set in CI.
- `EAPO_ANALYSIS_LAYOUT_HOLD_MS` — keeps the Editor open that many
  milliseconds (0 to 30000) after the analysis-dock layout test, so the
  window can be captured.
- `EAPO_FRAME_PROBE_LINGER` — `1` leaves the window of the
  `--frame-hittest-test` probe open after its checks, with the native child
  cascade still in place, so the title bar and the edges can be dragged by
  hand. Never set in CI.
- `EAPO_GALLERY_LANG` — renders the gallery with a shipped translation
  (for example `ko`) instead of the default untranslated English, to judge
  translated typography.
- `EAPO_GALLERY_OUTLINE_REPORT` — appends one TSV line per measured outline
  side to the named file while the gallery checks a skin's control contract
  (`Editor/gallery/GalleryOutlineCheck.cpp`): skin, mode, scene, state,
  class, name, properties, x, y, w, h, side, value, verdict, then the part's
  own state and the outline and outside colours. Exempt parts get one line
  with the exemption's reason; lit parts add a `colour` line with the
  largest delta-E between two of their sides. The file is appended to, not
  truncated.
- `EAPO_GALLERY_VST2_PLUGIN` / `EAPO_GALLERY_VST3_PLUGIN` /
  `EAPO_GALLERY_VST3_UPMIXER` — the test plug-ins the gallery's VST cards
  load (`TestVst2Plugin`, `TestVst3Plugin`, and a copy of the latter named
  `Upmixer.vst3`); `Invoke-EditorOffscreenTest.ps1` sets them. The
  `--vst-untouched-test` gate loads `EAPO_GALLERY_VST2_PLUGIN` as well.

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
- `EAPO_XT_TEST_IR_DIR` - set by `Tests/HybridConvTests/HybridConvTests.cpp`
  and `Tests/EditorLogicTests/ConfigImportTests.cpp` to check environment
  expansion in convolution paths. Not meant to be set by hand.
- `EAPO_XT_TEST_IMPORT_IR` - set by
  `Tests/EditorLogicTests/ConfigImportTests.cpp` to check that the import
  scanner collects an IR referenced through an environment variable.
  Not meant to be set by hand.
- `EAPO_XT_JUDGED_IR` - set and cleared by `assertJudgedIrCacheReuse` in
  `Tests/HybridConvTests/HybridConvTests.cpp`. It names the temporary IR that
  the test opens twice through a quoted environment-variable path to check
  decoded-cache reuse. Not meant to be set by hand.
- `EAPO_TEST_VST_METADATA` — makes `TestVst2Plugin` report the synthetic
  metadata variant named by the value, so `HybridConvTests` can exercise
  host-side metadata handling.
- `EAPO_TEST_VST_HOST_CALLBACKS` — set by `HybridConvTests`
  (`VstHostTests.cpp`) while it loads `TestVst2Plugin`: the fixture then
  calls the host's PRODUCT_NAME and SUPPORTS callbacks with null and valid
  pointers at load and refuses to load if any answer is wrong. Not meant to
  be set by hand.
- `EAPO_TEST_VST3_UI_THREAD_BOUND` — set to `1` while loading a fresh
  `TestVst3Plugin` bundle to bind its live components to the thread of the
  first initialization. Its panel refuses attachment on another thread and
  exported counters record lifecycle violations. Used only by host tests.
- `EAPO_TEST_VST3_HOST_CONTEXT` — set to `notimpl` by `HybridConvTests`
  (`Vst3HostTests.cpp`) while it loads a fresh `TestVst3Plugin` bundle: the
  fixture's factory returns `kNotImplemented` from `setHostContext` without
  keeping the context, then still creates and initializes its component. Not
  meant to be set by hand.
- `EAPO_TEST_VST_EDITOR` — set by `HybridConvTests` (`VstHostTests.cpp`)
  while it loads `TestVst2Plugin`: `fixed` gives the fixture a 320x200
  editor that does not take the content scale request, `scaled` one that
  does, and `late` one that drops a scale request received before its editor
  opens, as JUCE 6.0.0 to 6.1.2 do. Unset, the fixture has no editor. The
  Editor's `--vst-untouched-test` gate sets `fixed` too, to embed the
  fixture's panel under the offscreen platform. Not meant to be set by hand.
- `EAPO_TEST_VST_STATE` — set by the Editor's `--vst-untouched-test` gate
  while it loads `TestVst2Plugin`: `zip-clock` makes the fixture hand out its
  state as a ZIP archive whose file times move on every read, as the
  Kilohearts plug-ins do (#459), and `zip-clock-automate` also makes it call
  the host's automate while its state is being written. Unset, the state is
  the plain 16-byte blob. Not meant to be set by hand.
- `EAPO_XT_TEST_IMPORT_DIR` / `EAPO_XT_TEST_INCLUDE_DIR` — set by
  `EditorLogicTests` (`ConfigImportTests.cpp`) and `EngineOrchestrationTests`
  (`ConfigLoadTests.cpp`) themselves, so a test config can write
  `Include: %VAR%\file.txt` and check that the Editor import and the
  engine expand variables in Include paths. Not meant to be set by hand.

## Build / release

- `EAPO_CODEQL_MODEL_PACK` — set by `codeql.yml` to the path of
  `.github/codeql/eapo-models`, so the analyze step can pass the project's
  CodeQL model pack. Used only inside that workflow.
