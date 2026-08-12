# Superloopy frontend evidence receipt

Run: `20260812T014821Z-vst3-bus-layout-editor`

Implementation candidate: `befe7cf1e47caca0e15c3770e5df931cd2718b14`

Route: installed Windows desktop, native Qt Widgets, ModernCards, mouse and
keyboard. The existing five-skin styling system remains authoritative. SEO,
Web/browser, Lighthouse, crawlability, and marketing gates were explicitly
excluded because no Web owner or public content surface changed.

## Repository gates

- Configure/build — PASS: qmake regenerated the MSVC Release x64 AVX2 build;
  `nmake` produced `Editor.exe` with Qt 6.9.3. The repository wrapper's local
  `v145` request was unavailable on this machine, so native test projects were
  built with installed `v143`; CI retains the repository's `v145` gate.
- Qt Test/ctest — GAP with evidence: this repository has no Qt Test/ctest target
  for the Editor widget. Focused widget behavior is covered by the real
  `Editor.exe --selftest-vst` journey and a standalone EditorLogic model test.
- Affected suites — PASS: `EditorLogicTests` 2,967 checks;
  `HybridConvTests` 1,635 checks, with `Vst3HostTests` 101 checks;
  `Editor.exe --selftest-vst` 0 failures.
- Deterministic gallery — PASS: 1,370 English PNGs across five skins, two color
  modes, and registered states; 274 Korean studio PNGs; both in-test expected
  shot-count gates passed.
- Translation package — PASS: `lrelease Editor/Editor.pro`; Korean generated
  catalog contains the completed new VST bus strings. Other languages retain
  the project's existing unfinished fallback behavior.
- Lint/static — PASS where available: `git diff --check`. No repository C++
  formatter/static-analysis target is defined for this qmake scope.

## Claim-shaped surface evidence

Target: `windows-10-qt-editor`; platform: `windows`; environment:
EqualizerAPO-XT native Qt Widgets 6.9.3 Editor on Windows 10 Pro 19045 x64,
Release x64 AVX2, CustomStyle/Fusion, offscreen raster client pixels.

Owner: `native`. Claims: `interaction`, `visual`, `target`. Scope reason: the
changed paired selectors, visible negotiation status, VST2 repair action, and
their Windows Qt target integration are owned by the native Editor.

Proof:

- interaction — `app-automation-transcript.md` and the three full test logs;
- visual — `mobile-skin-gallery.png`, `mobile-state-gallery.png`,
  `mobile-ko-state-gallery.png`, and the curated original captures;
- target — `device-report.json`.

Verdict: PASS for the disclosed client-pixel, keyboard/pointer, serialization,
and deterministic-host claims. Native chrome and assistive-technology claims
remain explicitly unverified in `VISUAL_QA.md`.
