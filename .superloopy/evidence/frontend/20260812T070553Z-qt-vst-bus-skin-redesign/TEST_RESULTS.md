# Test results

- Qt 6.9.3 / MSVC Release x64 AVX2 qmake+nmake build: PASS.
- `EditorLogicTests.exe`: PASS, 2,967 checks.
- `Editor.exe --selftest-vst`: PASS, including five distinct skin factory views
  and shared semantic child controls.
- English five-skin gallery: PASS, 1,370 PNGs, exit 0.
- Korean five-skin gallery: PASS, 1,370 PNGs, exit 0.
- `lupdate` and `lrelease`: PASS; Korean `From`/`To` are translated as 입력/출력.
- `git diff --check`: PASS.

GitHub Actions is tracked separately against the exact pushed commit.
