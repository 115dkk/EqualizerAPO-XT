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
handling**: rotary or vertical drag tracking (the skin picks one, see the
gesture hook below), wheel, keyboard. Its `paintEvent` only collects a
`KnobState` and delegates painting:

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

`SkinManager::paintKnob` routes the widget to the active skin; a skin implements
the override on its `ISkin` subclass, whose definitions are grouped by visual
responsibility under `Editor/skins/<id>/`. `Skins.cpp` only holds the roster
lookup; concrete skin code stays inside its module folder.

### Knob gesture hook

```cpp
enum class KnobGesture { Rotary, VerticalDrag };
virtual KnobGesture knobGesture() const;   // default: Rotary
```

The widget still owns both gestures; the skin only names the one its
instrument is built for, because a gesture that contradicts the picture is
the discomfort itself (turning a drum in circles). `Rotary` follows the
pointer's angle about the centre (every arc knob). `VerticalDrag` follows
the pointer's vertical travel: the press grabs without moving the value,
`KnobTravel::RangePixels` (200 logical px, `Editor/widgets/KnobTravel.h`)
sweeps the whole range, Shift divides the rate by ten, the ends clamp with
no rubber band, horizontal motion is ignored, and the resting cursor is a
vertical arrow. A skin that paints a rolling surface derives its rotation
per range from the same constant so the surface moves with the pointer one
to one (minimal's register drum). The law is pinned by `EditorLogicTests`
(`KnobTravelTests.cpp`). `Editor --knob-specimen <outDir>` paints a skin's
knob for staged states (hover, drag, focus, disabled, a 50px squeeze) that
the gallery cannot stage.

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
- `typeBadgeStyle(info, typeColor, tokens)` /
  `typeBadgeInk(info, typeColor, badgeToken, tokens)` — the badge pair. The
  style string dresses `QLabel#FilterTypeBadge`; since feedback round 2 the
  badge content is the catalog pictogram
  (`FilterCardModel::badgeIconResource`, English monograms survive only as
  the fallback for unmapped lines), and because a tinted pixmap cannot follow
  the QSS `color` rule, the ink hook restates the same colour the style
  writes for text. Override both together or the glyph and the chip drift
  apart. `badgeToken` is the descriptor monogram (the biquad type code),
  which studio folds onto its band families.

QSS can already target rows per command type without code: the card frame and
header carry dynamic properties `filterKind` (lower-cased command),
`filterEnabled`, `selected`, `focused`, `scopeDepth`.

## List-chrome hooks (add row, insertion seam)

Legacy-cleanup round 3 replaced the green-icon `QToolBar` at the end of the
card list with two skinnable widgets. Both own all input handling (click,
Space/Return, focus, tooltips) and delegate every pixel:

```cpp
// ISkin (Editor/skins/ISkin.h)
virtual void paintAddRow(QPainter&, const QRect&, const ListChromeState&, const SkinTokens&) const;
virtual void paintInsertSeam(QPainter&, const QRect&, const ListChromeState&, const SkinTokens&) const;
```

`AddCardRow` (`Editor/widgets/AddCardRow.{h,cpp}`) is the persistent "add
card" row after the last card; the rect handed to the hook already carries
the cards' 8px column inset. `FilterInsertSeam`
(`Editor/widgets/FilterInsertSeam.{h,cpp}`) floats over the first card's top
margin and only paints while hovered — at rest it must stay invisible, which
is also why adding the hook changed no gallery shot. `ListChromeState`
carries `hovered/pressed/focused` and the widget's translated `label`. The
semantics (header `+` inserts BELOW its card, the seam is the only
front-insertion entry, no per-gap `+` chrome) are the shared insertion
contract in [docs/skins/README.md](skins/README.md); LegacyRows keeps the
frozen toolbar flow.

## GraphicEQ plot hook

The modern GraphicEQ card (`Editor/widgets/cards/GraphicEQCardEditor.{h,cpp}`)
does not reuse the legacy QGraphicsView stack at all. Its response lives in
`GraphicEQPlotWidget` (`Editor/widgets/GraphicEQPlotWidget.{h,cpp}`), which
follows the knob precedent: the widget owns the node model and every input
gesture (drag, double-click insert, Delete, selection, arrow nudges, wheel
dB zoom, right-drag pan; the frequency axis is pinned to 20 Hz – 20 kHz) and
delegates every pixel:

```cpp
// ISkin (Editor/skins/ISkin.h)
virtual void paintGraphicEqPlot(QPainter&, const GraphicEQPlotState&, const SkinTokens&) const;
```

`GraphicEQPlotState` hands the skin pre-mapped pixel geometry: the widget
rect and inner `plotRect`, the sampled response `curve`, `zeroY`,
`nodePositions` with selection/hover/focus indices, labelled grid lines,
`bandLocked` (15/31 layouts, where stems/bars are a legitimate reading) and
the cursor readout text. The default implementation is a quiet token-driven
instrument; each shipped skin overrides it wholesale (form in paint code,
QSS only tints the stock controls around it). Precise entry rides the
selected-band readout strip under the plot — `GraphicEQReadout`,
`GraphicEQBandCaption`, `GraphicEQReadoutLabel`, `GraphicEQFreqBox`/
`GraphicEQGainBox` (X1 value-scrub grammar) — plus `GraphicEQModeCombo`
(X5 paramSelector) and the `GraphicEQActionButton` row above. The frozen
LegacyRows GraphicEQ GUI keeps the original QGraphicsView stack untouched.

## Skin theme data for satellite executables

`Editor/skins/SkinThemeData.{h,cpp}` holds the behaviour-free half of the
skin system: id aliases (`resolveId`), the five token tables, QSS resource
paths, the `@TOKEN@` substitution, the token → `QPalette` mapping and the
Qt 6.10 combo-arrow override. The `ISkin` classes delegate their
`tokens()`/`qssResource()` here, so the tables cannot drift. DeviceSelector
compiles this one unit plus the aliased `.qss`/font resources
(`DeviceSelector/DeviceSelectorSkins.qrc`) and wears the Editor's stored
skin (`interface/skin`, default studio; heritage mode keeps the native
look).

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
`Editor/skins/<id>/cards/<Skin>ReferenceCardView.{h,cpp}`. Paths elide at paint
time (`Editor/widgets/ElidedLabel.h`), never at set time.

## VST bus strip hooks

The VST card's Input/Output contract is one shared instrument
(`Editor/widgets/cards/VSTBusStrip.{h,cpp}`): two format selectors joined by
a direction mark, with a compact negotiation verdict trailing them. The
strip follows the AudioKnob split - the widget owns every bit of behavior
(the layout popup menu, keyboard access, focus, enable/disable reasons,
accessibility) and delegates all painting:

```cpp
// ISkin (Editor/skins/ISkin.h)
virtual void paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const;
virtual void paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const;
```

`VstBusSelectorState` carries the role in two registers (the untranslated
`IN`/`OUT` token for engraving/terminal/board constitutions, the translated
caption for the friendly ones), the layout token in config grammar, and the
interaction flags. `VstBusFrameState` carries the child geometry plus the
verdict: either a short word or the negotiated pair split into input/output
texts, so each skin joins the pair with its own direction mark instead of
betting a font's arrow glyph. A tone without words is a lamp-only verdict:
an accepted explicit contract (the selectors already print the pair) and a
rejected one (the status line carries the sentence; a "Rejected" word next
to a danger lamp restated the lamp - maintainer judgement, r2).

Placement is the reference view's decision
(`ReferenceCardView::placeBusStrip`): the strip mounts beside the plugin
identity, in the row's horizontal slack, never as a stacked extra row.
Long-form bus messages (a rejection, VST2 stale keys, the legacy
StereoInput migration note) do not enter the strip; the host routes them
through the card's existing status line, so every skin's status idiom keeps
speaking.

## VST channel-fill rail hooks

The per-slot channel fill of a forced contract (`InputChannels` /
`OutputChannels`) renders as two rails inside the card - the input rail
under the row header, the output rail under the reference body - built from
one shared widget (`Editor/widgets/cards/VSTSlotFillRail.{h,cpp}`) over the
document model (`VSTSlotFillModel`). The same widget/paint split as the bus
strip:

```cpp
// ISkin (Editor/skins/ISkin.h)
virtual void paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const;
virtual void paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const;
```

Two constitutional constraints, both from the design gate:

- A fill cell picks a CHANNEL, the bus selector picks a LAYOUT, and the two
  must not read as the same control. Every skin answers the cell with a
  different register than its bus selector (scribble well vs. glass pane,
  patch socket vs. latch cap, posting rule vs. boxed key, outlined pill
  vs. filled pill; minimal stays bare ink in both, and that is its answer).
- The fold latch is the rail frame's affordance, never one more dropdown:
  a lit console switch (studio), the latch button with its pilot LED
  (rack), a reverse-video token (minimal), a dot toggle (soft), a boarding
  gate cell whose collapsed state is a dashed cancellation (matrix).

Cell states carry the slot role token, the assigned channel, and three
verdicts the skin must voice: `silent` (the `-` slot), `defaulted` (no
explicit list; render quieter than a committed value) and `missingChannel`
(the committed channel does not resolve into the channels selected at this
row - danger ink, the engine would pass audio through over it). The rail
state carries the cells' bounding rect, the latch rect (null when the card
has a single rail - a single rail never folds) and the collapsed flag.

The rails' presence rules live in `VSTSlotFillModel`, not in skins: an
`Auto` side has no rail, only a two-rail card gets the latch, and a
collapsed card keeps just the input rail's latch as its slim reminder.

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
(`#MultiConvolutionMappingCaption` in every skin's QSS) and a `+` entry that
adds output channels (a new name becomes a virtual channel, like Copy). Data
rides `Editor/widgets/routing/MultiConvolutionRoutingAdapter` both ways;
without a readable IR file the whole mapping block hides, because the simple
form ("every file channel") has no known expansion to edit and the reference
view's own status grammar already says what is missing.

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

For every skin × {dark, light} it renders eighteen representative rows — a
parametric filter (`Filter 1: ON PK ...`), a high-shelf with its three knobs,
a peaking filter at 0 dB (bipolar gain at its neutral detent), a `Preamp:`
row (the bare knob + value scrub pair — the row that shows whether a skin
seats custom widgets directly on its surface), an `Include:`
row (resolved), a nested `Include: Surround\...` row (the location line), a
missing `Include:` row (the broken-reference transition with the Locate
entry), a `VSTPlugin:` row (unresolvable library - the missing/named-device
state), two `Device:` rows over four synthetic endpoints (a named selection —
engaged playback switch + engaged capture well beside an idle endpoint, with
the APO-less endpoint behind the reveal toggle — and the `all` master over
powered-down chips), a `Channel:` row, a comment row (`# ...`, the note
card), a `Stage:` row (the two-lane stage card: a captioned Playback lane
with the pre-mix → post-mix chain, and a Recording lane holding capture), an
empty `Copy:` row, a
populated `Copy:` row (mixed factors and a virtual target — the routing
views' judged scene), a `Convolution:` row, two `MultiConvolution:` rows
(populated and freshly inserted empty), a `GraphicEQ:` row (the modern
GraphicEQ card — the clean-install first impression) and two raw-text rows
(a bare note line and an `If:` command, the TXT presentation) — in three
states: `normal`, `hover` (hover-equivalent via `Qt::WA_UnderMouse`), and
`disabled` (the line commented out, which is the product's real disabled
state). The reference rows resolve against synthetic target files the gallery
writes under `<outDir>/refs/` (`example.txt`, a 100 ms mono `example.wav`, a
100 ms 4-channel `brir.wav`), so the healthy cards render deterministic
readouts; the gallery also sets `EAPO_SKIN_GALLERY=1`, which the card editors
honour by skipping the audio-service ACL probe (scratch files have no
meaningful ACL story). The filter picker is captured in three states
(`normal`, `hover`, `empty`; pickers that do not implement
`FilterPickerView::galleryShowcase` repeat their normal look), plus one shot
each for the toolbar, title bar, menu bar and an open menu, two for the
add-card row (`addrow` normal/hover), one for the insertion seam's hover
reveal (`seam`) and one for the update toast (`toast`). Output names are
stable: `<skin>_<dark|light>_<row>_<state>.png`,
5 × 2 × (21 × 3 + 12) = 750 PNGs
for a full run; the run self-checks the count, so adding a gallery row needs
no external count update. A row shot fails the render (non-zero exit) if a
visible horizontal scrollbar is found inside the row — rows must fit the
960px gallery viewport in every skin. Exit code 0 means every PNG was
written; unknown skin ids fail loudly instead of falling back to studio.

CI runs the gallery on the primary x64-avx2 variant and uploads the PNGs as
the `skin-gallery` artifact, so a PR's visual state can be reviewed without a
local build.

Determinism notes: the gallery runs before translators load (English strings),
applies each skin itself (`SkinManager::applySkin` swaps the stylesheet and
derives the palette), and renders at device pixel ratio 1 on the
offscreen platform. PNGs from the same machine and build are byte-stable, so
`Get-FileHash` comparisons prove pixel identity; PNGs from different machines
may differ slightly in font rasterization.

## Lane geometry and tick label boxes

`CommandRowInfo` carries the row's lane geometry: `laneUnit` (one indent band),
`laneCount` (how many bands are drawn left of the card face), `cardLeft`, and
`laneCenter(level)`. `paintScopeGutter` paints in that space, so a skin answers
what a lane looks like and never recomputes where it is. The rule that a branch or
tail row mounts one unit deeper than its head is already folded into `laneCount`,
because the same call sets the row widget's own left margin - a gutter can no
longer disagree with the card face beside it.

`skinXTickLabelRect` / `skinYTickLabelRect` in `SkinPaint.h` build a tick label
box centred on a grid line. The y variant keeps its inset and width as arguments:
the skins use 4, 5, 6 and 8 px and nobody decided they should differ, but nobody
decided they should agree either, so settling it is a skin round's call rather
than a refactor's.

Colour tokens answer to an `@TOKEN_RGB@` form as well as `@TOKEN@`, expanding to
the three channels so a sheet can write `rgba(@ACCENT_RGB@, 0.30)`. QSS has no
variables and its `rgba()` wants numbers, so this is the only way a sheet holds a
token at partial alpha instead of writing the palette value out by hand.

`prepareCommandRow` receives `SkinTokens` like every other hook. It was the one
that did not, and all five skins reached for `SkinManager::instance()->tokens()`
inside it instead.

## Adding a skin

Six files, and the first one is the only list of which skins exist.

1. **`Editor/skins/SkinThemeData.cpp`** — add an entry to `roster()`: the id as it
   will be stored in the registry, the base name of its `.qss` pair, and its token
   function. Everything derived from the roster follows automatically: the Editor's
   menu, the token and style-sheet lookups, Device Selector's shot harness, and the
   `testTheSkinRosterIsTheOneList` check.
2. **`Editor/skins/<Name>Skin.cpp`** — the `ISkin` subclass, one translation unit
   per skin, plus its `<name>Skin()` accessor declared in `SkinSupport.h`.
3. **`Editor/skins/Skins.cpp`** — one line in `implementationFor()` mapping the id
   to that accessor. A roster id missing from here is logged and left out of
   `Skins::all()`, rather than silently drawn as Studio.
4. **`Editor/skins/<name>_light.qss` / `<name>_dark.qss`** plus their entries in
   the resource file. Every `@TOKEN@` the sheets use has to be produced by the
   token table, which `testEverySkinSheetResolvesAllThemeTokens` checks.
5. **`Editor/MainWindowParts/MainWindow.Preferences.cpp`** — the translated display
   name, keyed by id. A missing name shows the raw id in the menu.
6. **`DeviceSelector/skins/<Name>DeviceSkin.cpp`** and one line in
   `DeviceSkinPainter::forSkin` — the device dialog's counterpart. Without it that
   dialog draws the new skin as Studio, which is the one place a partial roster is
   still tolerated.

And the constitution: **`docs/skins/<name>.md`** records what the skin is for and
what it must not do. `docs/skins/README.md` says why that document exists before
the code does.

Until this list existed, adding a skin meant editing eighteen places, and missing
one did not fail. `resolveId()` returns `"studio"` for an id it does not know, so a
skin that compiled, registered and appeared in the menu was drawn as Studio with no
error anywhere.
