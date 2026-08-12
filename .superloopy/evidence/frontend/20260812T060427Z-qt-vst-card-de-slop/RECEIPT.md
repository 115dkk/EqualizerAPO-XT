# Superloopy frontend evidence receipt

Run: `20260812T060427Z-qt-vst-card-de-slop`

Target: Equalizer APO XT native Windows Qt Widgets editor. SEO, browser, public
content, and marketing surfaces are explicitly out of scope.

## Correction

- Structured VST cards no longer render their serialized `VSTPlugin:` command.
- Studio and Minimal raw-preview strips and Matrix caption strips are limited to
  true raw/dynamic fallback rows.
- VST identity is a plug-in filename plus the selected bus pair; absolute
  development paths and the `ABS` badge are not part of the card presentation.
- Status text reports the current outcome without repeating parser/backend
  diagnostics. The full source remains available through the existing edit
  action.
- The gallery now fails if a structured VST row exposes
  `FilterCardRawPreview` or `MatrixRowCaption` in normal, hover, or disabled
  states.

## Evidence verdict

- Build: PASS — Qt 6.9.3, MSVC x64 Release AVX2.
- Focused logic: PASS — `EditorLogicTests`, 2,970 checks.
- Runtime journey: PASS — `Editor.exe --selftest-vst`, exit 0.
- English gallery: PASS — 1,370 PNGs, five skins, light/dark and registered
  states.
- Korean gallery: PASS — 274 Studio PNGs, light/dark and registered states.
- Visual review: PASS — curated mobile sheets and source captures contain no
  raw VST command, absolute repository path, or `ABS` badge.
- Patch hygiene: PASS — `git diff --check`.

See `VISUAL_QA.md`, `TEST_RESULTS.md`, `UX_CONTRACT.md`, and
`app-automation-transcript.md` for the supporting record.
