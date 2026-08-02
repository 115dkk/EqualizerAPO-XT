# Refactoring Baseline

This pass turns the initial "prepare the codebase before serious builds" TODO into explicit, reviewable structure. The work is spread across focused commits instead of one broad rewrite, but the baseline is now concrete:

- Audio regression tests exist (today in `Tests/AudioRegressionTests`; at the time of this baseline they started inside `Tests/HybridConvTests`) and are wired into the solution and CI.
- GUI/update logic tests exist in `Tests/EditorLogicTests` and cover non-audio helper behavior without launching Qt dialogs.
- Convolution path handling is isolated in `ConvolutionFilePath` and `ConvolutionPathHelper` instead of being duplicated in parser and GUI code.
- Update release-note formatting is isolated in `UpdateInfoFormatter`, making escaping and skip-version behavior testable.
- Velopack/GitHub release feed parsing is isolated in `VelopackUpdateInfo`, keeping network I/O in `main.cpp` and pure update selection in tests.
- SIMD build expectations, runnable test policy, and channel names are documented in `docs/SimdBuildMatrix.md`.

The baseline intentionally leaves the low-level audio convolution internals small and targeted. Bugs there are guarded by regression tests first, then changed only where a failing test points.

Verification used during the pass:

- `Tests\HybridConvTests\x64\Release\HybridConvTests.exe`
- `Tests\EditorLogicTests\x64\Release\EditorLogicTests.exe`
- qmake/nmake build of `UpdateChecker` with an injected `EAPO_UPDATE_CHANNEL`
- `git diff --check`
