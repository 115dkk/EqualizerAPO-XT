# Test results

## Focused checks

- `EditorLogicTests.exe` — PASS, 2,970 checks. Includes exact clean-summary
  coverage for `Upmixer.vst3 · Stereo → 7.1`, absence of `Library`, `Input`, and
  `C:\`, and the empty `No plugin selected` state.
- `Editor.exe --selftest-vst` with `QT_QPA_PLATFORM=offscreen` — PASS, exit 0.
- Incremental qmake/nmake Release x64 AVX2 build — PASS with Qt 6.9.3 and MSVC
  14.44.
- `lupdate Editor/Editor.pro` and `lrelease Editor/Editor.pro` — PASS; updated
  `.ts` and `.qm` catalogs are included.
- `git diff --check` — PASS.

## Gallery gate

- English: 1,370 PNGs = 274 registered captures × five skins.
- Korean: 274 Studio PNGs.
- Both gallery processes exited 0 after the new structured-row assertion ran
  across normal, hover, and disabled VST states.

The repository's full GitHub Actions matrix is tracked separately against the
exact pushed commit.
