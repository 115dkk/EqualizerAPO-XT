# Skin program — Phase 2 integration report

Closes the three-phase skin program (issues #66 → #67 → #68). Five isolated
agents each implemented one skin on `skin/<id>` branches (Phase 1); this
report records the judging and the integration of those branches into one
tree (Phase 2).

## Branches integrated

Merged into `integrate/skins` cheapest-first, exactly in this order:

| order | branch | head | scope (files outside `Editor/skins/` + own QSS) |
|---|---|---|---|
| 1 | `skin/minimal` | 5921455 | none |
| 2 | `skin/matrix` | 0e5e6fd | `SkinManager.{h,cpp}`, `widgets/CommandRowFrame.cpp`, `widgets/FilterCardRow.cpp` (new `typeBadgeStyle` hook + `hovered` flag) |
| 3 | `skin/studio` | d75a104 | `widgets/CommandRowFrame.cpp` (`hovered` flag) |
| 4 | `skin/soft` | b1dad2d | none |
| 5 | `skin/rack` | 8415116 | `Editor.pro` (registers `skins/RackChrome.{h,cpp}`) |

Merge conflicts were limited to `Skins.cpp` include unions and helper-block
ordering, plus one genuinely interesting collision: **studio and matrix both
added the same `CommandRowInfo.hovered` paint-time flag independently**
(isolation working as designed); the merge kept a single copy. No branch
touched `filters/` (command codecs), `AudioKnob` input handling, or any other
forbidden area.

## Differentiation gate (the core check)

Rule: an item FAILS if any two skins differ only by palette. Judged from the
contact sheets (below) and the implementations. **All seven items pass for
all five skins.** Condensed answers:

| # | item | studio | minimal | soft | rack | matrix |
|---|---|---|---|---|---|---|
| 1 | type announcement | border treatment (solid/dashed/gradient) + painted signal lamp/halo | leading ASCII glyph (`~`, `>>`, `[]`) + bare mono token | rounded-square colour tile + per-type body shape | per-type plate finish + jacks/nameplate + engraved ear designation | square mono type-code cell + per-type body layout |
| 2 | hover | glass brightens, glow intensifies (painted) | one background step | card lifts one elevation step | faceplate sheen + amber hardware edges | row band + coordinate-column band (crosspoint) |
| 3 | disabled | glass with the light off (alpha drop, reflection/lamp gone) | `#` marker glyph + one step below rest | sleeping slot (dashed silhouette, sunk flush) | powered-down unit (LED dark, dim film, hardware stays) | cancelled departure (dashed border, amber hollow lamp, strikethrough) |
| 4 | Include row | path chip (pill) on dashed glass card | one text line, payload brighter than command | rounded breadcrumb chip on body tray | patchbay insert unit with painted jacks | `> source: <path>` mono board line |
| 5 | VST row | softly glowing module (gradient border + halo) | same single-line grammar as Include | app-store card (icon tile + vendor caption) | rack unit with riveted brass nameplate | external-device entry with IN/OUT port strip |
| 6 | corner/edge | 8px glass, lighter top edge / darker sunken edge | radius 0, 1px hairlines, no header plate | 12px cards, pills, faked two-step shadows | 3px machined corners, bezels, grooves, four screws | radius 0, 1px rules, 3px status rail, 24px grid texture |
| 7 | hierarchy lead | luminance backed by weight | text brightness, one mono size | size and whitespace (48px headers) | physical depth (raised/flat/recessed) | grid position, uniform type |

The closest structural pair is studio/soft (both keep the stock card
silhouette); they still separate by radius language, elevation strategy
(alpha-glass with top-edge reflection vs two-step elevation), knob vocabulary
(thin glowing arc vs largest soft-bodied knob) and the raw-preview strip
(studio keeps it, soft removes it). Verdict: pass, worth rechecking whenever
either skin is revised.

Knob vocabularies (Annex K) are five distinct instruments: glowing arc
(studio), number-first flat circle (minimal), large pastel handle (soft),
pointer knob over panel-printed scale (rack), LED segment ring + boxed numeric
cell (matrix). Bipolar gain knobs anchor mid-scale distinctly in each.

## Technical review per branch

All five branches: build passed, gallery rendered (24 PNGs each), scope
clean, serialization untouched, cppcheck 2.21.0 gate zero findings (verified
again on the integrated tree, including the new 500-line `RackChrome`
painter). Shared-hook additions (`hovered`, `typeBadgeStyle`) follow the
neutral-default rule; neutrality was proven empirically, not assumed — see
below.

## Interference verification (per merge step)

After every merge the full 120-PNG gallery was rendered and SHA-256-compared:
skins not yet integrated had to stay pixel-identical to the v1.18.0 main
baseline, and integrated skins had to stay pixel-identical to their own
branch gallery. Result at every one of the five steps: **0 mismatches** (600
comparisons total). The hook extensions changed nothing for skins that do not
override them.

Default skin: `studio` (unchanged; `SkinManager` and the startup settings
default both verified). QSS identity headers: all ten sheets carry their
constitution names (the `minimal` skin intentionally keeps its historical
`precision_*.qss` file names).

Skin-switch performance: switching still recreates rows via the existing
`FilterTable::updateGuis()` path; no skin added per-switch work beyond
construction-time painting, and full-tree QSS re-polish behavior is
unchanged. The offscreen gallery (which performs five full skin switches per
run) completes in seconds, same as before the program.

## Judging material

Contact sheets (the same row across all five skins, normal and disabled,
dark and light) are hosted on the `gallery-phase2` branch (never merge it):

`https://raw.githubusercontent.com/115dkk/EqualizerAPO-XT/gallery-phase2/sheets/sheet_<dark|light>_<filter|shelf|include|vst>_<normal|disabled>.png`

Per-skin Phase 1 galleries are browsable in issue #72 (hosted on
`gallery-phase1`). To regenerate any of it:
`Editor --skin-gallery <outDir> [--skin-gallery-skins id,...]` (see
`docs/skin-hooks.md`).

## Interaction-layer wishes (follow-up candidates)

Collected from the five agents, deduplicated; none implemented in this
program (presentation-only rule):

1. **`KnobState` label/unit fields** — rack (engraved label under the knob),
   matrix (unit-suffixed numeric cell), minimal (engineering-value readout)
   all hit the same missing API; the host would fill it from the `.ui`
   captions/spin boxes.
2. **Legacy `.ui` dials pass no `valueText`** — blocks studio's fade-in
   readout and soft's per-knob captions on shelf rows; same root as (1).
3. **VST status label colours via `QPalette` `Qt::red`/`Qt::black`** — should
   route through `SkinTokens` (`danger`/`text`); unreadable on dark cards.
4. **Gallery harness artifacts** — the offscreen synthetic cursor at (0,0)
   gives row 1's frame hover chrome in its `normal` shot, and
   `FilterTable::setLines` focuses row 1 so its focus ring is visible. Worth
   clearing `WA_UnderMouse`/focus before the normal grab.
5. **Raw-preview strip styling is hardcoded in `FilterCardRow`** — a skin
   hook would let rack render it as a serial-number label (rack disables it
   via `showRawPreview` for now).
6. **FilterTable-level hover hook** — matrix's crosspoint column band can
   only paint inside the hovered card; a table-level band would cross the
   whole board.
7. Smaller: single-line Include/VST body layout (minimal), VST vendor
   metadata plumbing (soft), no condensed font in the repo (rack approximates
   with letter-spaced DM Sans).
