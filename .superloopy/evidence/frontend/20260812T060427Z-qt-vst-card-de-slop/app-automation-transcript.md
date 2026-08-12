# App automation transcript

1. Built the real Qt 6.9.3 Release x64 AVX2 `Editor.exe` through the repository's
   qmake/nmake project.
2. Ran `Editor.exe --selftest-vst` offscreen; process exited 0.
3. Staged the real deterministic `TestVst3Plugin.vst3` and `TestVst2Plugin.dll`
   fixtures through the gallery environment.
4. Ran the English skin gallery for `studio,minimal,soft,rack,matrix`; process
   exited 0 and produced 1,370 PNGs.
5. Ran the Korean Studio gallery with `EAPO_GALLERY_LOCALE=ko_KR`; process exited
   0 and produced 274 PNGs.
6. The gallery's structured-row gate checked that neither
   `FilterCardRawPreview` nor `MatrixRowCaption` was visible for each VST normal,
   hover, and disabled capture.
7. Inspected the accepted Studio/Korean/Matrix captures and the composed mobile
   skin/state sheets at original pixel dimensions.
