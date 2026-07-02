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

## Reference-card view hook

Rows whose subject is an external file (Include, Convolution,
MultiConvolution, VSTPlugin) render their body through a per-skin view,
following the Copy routing-renderer precedent:

```cpp
// ISkin (Editor/skins/ISkin.h)
virtual ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const;
```

The host editors (`Editor/widgets/cards/{Include,Convolution,MultiConvolution,VST}CardEditor`)
own all behavior - path resolution, file dialogs, plugin lifecycle, dependency
import - and describe themselves through `ReferenceCardState`
(`Editor/widgets/cards/ReferenceCardView.h`): primary name, as-written
location, missing flag, absolute-path flag, VST2/VST3 format badge, the
impulse-response readout list and one status line. Views print the location
through `ReferenceCardState::locationPrefix()` - the directory closed by its
trailing separator (`Surround\`) - so the folder always reads as what
contains the file; a bare folder name hanging off the name depicted the
containment upside down (matrix's `@ <dir>` marker already states the
location and stays as is). Views own structure and
presentation only; the base class owns the shared inline path-edit mode and
the name-activation plumbing (clicking the name opens the target / plugin
panel). Hosts hand action buttons over with semantic roles
(`addActionButton`) and never lose control of their behavior or visibility;
the Browse button doubles as the "Locate..." recovery entry while the
reference is missing. The default is the neutral
`DefaultReferenceCardView`; the five shipped skins override it in
`Editor/skins/cards/<Skin>ReferenceCardView.{h,cpp}`. Paths elide at paint
time (`Editor/widgets/ElidedLabel.h`), never at set time.

## Routing renderer hook

Copy's per-skin routing view generalizes to any command whose body is a
source→target mapping:

```cpp
// ISkin (Editor/skins/ISkin.h) -> IRoutingRenderer (Editor/widgets/routing/IRoutingRenderer.h)
virtual IRoutingRenderer* routingRenderer() const;
RoutingView* IRoutingRenderer::create(assignments, channelNames, portModel, parent);
```

`RoutingPortModel` selects between the two shapes. The default reproduces
Copy: both sides grow from the assignments plus every device channel, and
every connection carries an editable factor. A non-empty
`portModel.fixedSources` puts the view in fixed-source mode: the source side
is exactly that list (MultiConvolution passes the IR file's channels,
`"0".."N-1"`), no other source is offered, sources keep the solid port
styling (they are ports, not virtual channels), and `allowFactors == false`
reduces interaction to connect/disconnect (no gain labels, captions or
editors; a double-click on a chip/pill removes it where the grid views
toggle by click). The MultiConvolution card
(`Editor/widgets/cards/MultiConvolutionCardEditor`) hosts the view under its
reference card with a `Channel mapping` caption strip
(`#MultiConvolutionMappingCaption` / `#MultiConvolutionMappingHint` in every
skin's QSS) and a `+` entry that adds output channels (a new name becomes a
virtual channel, like Copy). Data rides
`Editor/widgets/routing/MultiConvolutionRoutingAdapter` both ways; without a
readable IR file the card shows the hint instead of a view, because the
simple form ("every file channel") has no known expansion to edit.

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

For every skin × {dark, light} it renders thirteen representative rows — a
parametric filter (`Filter 1: ON PK ...`), a high-shelf with its three knobs,
a peaking filter at 0 dB (bipolar gain at its neutral detent), a `Preamp:`
row (the bare knob + value scrub pair — the row that shows whether a skin
seats custom widgets directly on its surface), an `Include:`
row (resolved), a nested `Include: Surround\...` row (the location line), a
missing `Include:` row (the broken-reference transition with the Locate
entry), a `VSTPlugin:` row (unresolvable library - the missing/named-device
state), a `Device:` row, an empty `Copy:` row, a `Convolution:` row and two
`MultiConvolution:` rows (populated and freshly inserted empty) — in three
states: `normal`, `hover` (hover-equivalent via `Qt::WA_UnderMouse`), and
`disabled` (the line commented out, which is the product's real disabled
state). The reference rows resolve against synthetic target files the gallery
writes under `<outDir>/refs/` (`example.txt`, a 100 ms mono `example.wav`, a
100 ms stereo `brir.wav`), so the healthy cards render deterministic readouts;
the gallery also sets `EAPO_SKIN_GALLERY=1`, which the card editors honour by
skipping the audio-service ACL probe (scratch files have no meaningful ACL
story). The filter picker is captured in three states (`normal`, `hover`,
`empty`; pickers that do not implement `FilterPickerView::galleryShowcase`
repeat their normal look), plus one shot each for the toolbar, title bar,
menu bar and an open menu. Output names are stable:
`<skin>_<dark|light>_<row>_<state>.png`, 5 × 2 × (13 × 3 + 3 + 4) = 460 PNGs
for a full run; the run self-checks the count, so adding a gallery row needs
no external count update. A row shot fails the render (non-zero exit) if a
visible horizontal scrollbar is found inside the row — rows must fit the
960px gallery viewport in every skin. Exit code 0 means every PNG was
written; unknown skin ids fail loudly instead of falling back to studio.

CI runs the gallery on the primary x64-avx2 variant and uploads the PNGs as
the `skin-gallery` artifact, so a PR's visual state can be reviewed without a
local build.

Determinism notes: the gallery runs before translators load (English strings),
applies each skin itself (stylesheet + palette via
`GUIHelper::applySkinPalette`), and renders at device pixel ratio 1 on the
offscreen platform. PNGs from the same machine and build are byte-stable, so
`Get-FileHash` comparisons prove pixel identity; PNGs from different machines
may differ slightly in font rasterization.
