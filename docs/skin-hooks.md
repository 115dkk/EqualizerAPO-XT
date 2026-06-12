# Skin hooks and the screenshot gallery

Phase 0 of the skin program (issue #66) added two structural hooks to `ISkin`
and an offscreen screenshot gallery. This note is the reference for the Phase 1
skin agents (issue #67) and the Phase 2 integrator (issue #68).

This file covers the *mechanics* (hook contracts, gallery, proof rules). The
*design philosophy* of each skin — what it believes, what it forbids, and how a
new UI element must answer in its language — lives in
[docs/skins/](skins/README.md), one constitution per skin. Read the relevant
constitution before styling anything skin-specific.

## Knob paint hook

`AudioKnob` (`Editor/widgets/AudioKnob.{h,cpp}`, a `QDial`) owns **all input
handling**: rotary drag tracking, wheel, keyboard. Its `paintEvent` only
collects a `KnobState` and delegates painting:

```cpp
// ISkin (Editor/skins/ISkin.h)
virtual void paintKnob(QPainter& painter, const QRect& rect,
	const KnobState& state, const SkinTokens& tokens) const;
```

`KnobState` carries `value/minimum/maximum`, the resolved `ratio` (0..1), a
`bipolar` flag, the optional centred `valueText`, and
`enabled/hovered/dragging/focused`. Notes:

- **Bipolar vs unipolar.** Gain knobs (Preamp card, BiQuad gain dial) set
  `bipolar = true`; frequency and Q dials stay unipolar. A skin must render
  the two kinds distinguishably (e.g. the value arc grows from the centre for
  bipolar knobs).
- **`valueText` may be empty.** Promoted legacy dials (BiQuad, Delay,
  LoudnessCorrection `.ui` files promote `QDial` to `AudioKnob`) display their
  value in a separate spin box and map the dial to log-scaled steps, so
  painting `value` for them is wrong. Only paint a number when `valueText` is
  non-empty, or derive a display value from your own formatting of `ratio`.
- **Geometry.** Promoted legacy dials are 100x66; the card knob is 74x74. Keep
  the knob round by working inside a centred square (see the default
  implementation in `Editor/skins/ISkin.cpp`).
- The default implementation reproduces the pre-hook rendering
  pixel-identically and deliberately ignores the hover/drag/focus flags.

`SkinManager::paintKnob` routes the widget to the active skin; a skin
implements the override on its `ISkin` subclass in `Editor/skins/Skins.cpp`.

## Command-row chrome hook

Rows are recreated on every skin/dark switch (`FilterTable::updateGuis()`), so
per-row chrome is built at construction time. `CommandRowInfo`
(`Editor/skins/ISkin.h`) identifies a row: descriptor `type` (`"biquad"`,
`"include"`, `"vst"`, `"copy"`, ...), lower-cased `command`, a `legacyRow`
flag, and `enabled/selected/focused/depth`. Four hooks, all with
appearance-preserving defaults:

- `cardFrameStyle(info, tokens)` / `cardHeaderStyle(info, tokens)` — the
  inline stylesheets for `QFrame#FilterCardRow` and
  `QWidget#FilterCardHeader`, re-evaluated whenever the row's
  selected/focused/enabled state changes. The defaults are the shared
  token-driven strings every skin used before (including the
  `tokens.cardRailWidth` accent rail). Override these to give command types
  their own frame/header treatment.
- `prepareCommandRow(info, card, header, body)` — called once per
  construction. For modern card rows `card`/`header`/`body` are the
  `CommandRowFrame`, the header strip and the body stack. The Include/VST
  card editors and the legacy Include/VST rows also consult the hook with
  only `body` set (and `legacyRow = true` for the legacy pair). Use it to set
  dynamic properties for QSS or to attach extra chrome widgets.
- `paintCardChrome(painter, rect, info, tokens)` — painted decoration drawn
  by `CommandRowFrame` (`Editor/widgets/CommandRowFrame.{h,cpp}`) after the
  QSS background/border and before child widgets. Use for rails, screws,
  per-type markers that QSS cannot express.

QSS can already target rows per command type without code: the card frame and
header carry dynamic properties `filterKind` (lower-cased command),
`filterEnabled`, `selected`, `focused`, `scopeDepth`.

## Screenshot gallery

The Editor has a headless gallery mode used to prove appearance-preserving
changes and to produce judging material:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
# Qt bin + fftw/libsndfile DLL directories must be on PATH; velopack_libc.dll
# must sit next to Editor.exe (copy deps\velopack_libc\lib\velopack_libc_win_x64_msvc.dll).
# If windeployqt has run on the build dir, the deployed app dir only carries
# the qwindows platform plugin and the offscreen platform fails to load (the
# Editor then hangs on a fatal-error dialog). Point the plugin search at the
# full Qt install in that case:
#   $env:QT_QPA_PLATFORM_PLUGIN_PATH = "<QT_ROOT>\plugins\platforms"
.\build-Editor-x64\release\Editor.exe --skin-gallery <outDir> [--skin-gallery-skins studio,rack]
```

For every skin × {dark, light} it renders six representative rows — a
parametric filter (`Filter 1: ON PK ...`), a high-shelf with its three knobs,
a peaking filter at 0 dB (bipolar gain at its neutral detent), an `Include:`
row, a `VSTPlugin:` row and an empty `Copy:` row — in three states: `normal`,
`hover` (hover-equivalent via `Qt::WA_UnderMouse`), and `disabled` (the line
commented out, which is the product's real disabled state). The filter picker
is captured in three states (`normal`, `hover`, `empty`; pickers that do not
implement `FilterPickerView::galleryShowcase` repeat their normal look), plus
one shot each for the toolbar, title bar, menu bar and an open menu. Output
names are stable: `<skin>_<dark|light>_<row>_<state>.png`,
5 × 2 × (6 × 3 + 3 + 4) = 250 PNGs for a full run. A row shot fails the
render (non-zero exit) if a visible horizontal scrollbar is found inside the
row — rows must fit the 960px gallery viewport in every skin. Exit code 0
means every PNG was written; unknown skin ids fail loudly instead of falling
back to studio.

CI runs the gallery on the primary x64-avx2 variant and uploads the PNGs as
the `skin-gallery` artifact, so a PR's visual state can be reviewed without a
local build.

Determinism notes: the gallery runs before translators load (English strings),
applies each skin itself (stylesheet + palette via
`GUIHelper::applySkinPalette`), and renders at device pixel ratio 1 on the
offscreen platform. PNGs from the same machine and build are byte-stable, so
`Get-FileHash` comparisons prove pixel identity; PNGs from different machines
may differ slightly in font rasterization.
