# VST3 bus-layout editor UX contract

## Baseline and target

- Evidence — affected users: EqualizerAPO-XT users who host VST3 upmixers,
  downmixers, and height-channel processors whose main input and output layouts
  differ. Issue #216 records the real OpenSpatial `Stereo -> 7.1` case.
- Evidence — job and outcome: choose the paired `VSTPlugin:` `Input` and
  `Output` layouts, see whether the loaded plug-in accepted the pair, and save
  the same contract without hand-editing configuration text.
- Evidence — target: the installed Windows desktop Editor, native Qt Widgets,
  qmake, mouse and keyboard, with `ModernCards` as the maintained default UI.
  `docs/FilterListUiPolicy.md` freezes `LegacyRows`; that path retains opaque
  round-trip compatibility and receives no new controls.
- Evidence — Qt versions: CI pins Qt 6.10.1 in
  `.github/simd-variants.psd1`; the available local SDK is Qt 6.9.3. The qmake
  project does not declare a separate minimum Qt version, so the minimum is
  unknown and the change is constrained to APIs present in both named builds.
- Evidence — appearance owner: the existing QSS-led five-skin card subtree.
  Stock `QComboBox`, labels, and tool buttons keep each skin's current style;
  semantic status colors come from the active `SkinTokens` owner.
- Evidence — baseline: `baseline/studio_dark_vst_normal.png` and
  `baseline/studio_light_vst_normal.png` show that the card currently exposes
  only library identity/recovery and panel actions. `Input`/`Output` are hidden.
- Candidate — implementation and final receipts are bound to
  `befe7cf1e47caca0e15c3770e5df931cd2718b14`.
- Scope exclusion: this is native Qt UI. Browser/Web, crawlability, Lighthouse,
  and SEO are not applicable and are not selected Superloopy gates.

## Journeys and invariants

### VST3 contract journey

1. The user inserts or opens a VST Plugin card and selects a library.
2. After the module loads, its exported ABI—not its extension—determines
   whether the two selectors are enabled.
3. Changing either selector establishes the required paired contract. Both
   values serialize, including `Auto` on either side.
4. The Editor applies the pair to its real host instance and reports the
   accepted input/output layouts and channel counts. Rejection is explicit and
   says that the audio engine will pass the row through.
5. The stored `ChunkData` or parameter map remains unchanged.

Invariants:

- `Input` and `Output` are one atomic contract; a one-sided serialized state is
  never produced.
- `Auto` is a real selectable value, not omission once the user edits the pair.
- A visible success is based on the real plug-in instance accepting and
  reporting consistent bus metadata; handler invocation alone is not success.
- Bus selectors cannot mutate arrangements while an embedded plug-in editor is
  actively processing. Closing the embedded panel restores editing.
- Layout rejection never silently falls back to another named layout.

### VST2 recovery journey

1. Once the loaded ABI is VST2, both selectors are disabled.
2. Hover help on the controls and their containing panel explains that VST2
   does not support explicit bus layouts.
3. If a hand-written or migrated contract remains, the card says it is ignored
   and offers one explicit `Remove layouts` action.
4. Removal is a document mutation covered by the Editor's existing Undo model.

### Legacy migration journey

- A loaded `StereoInput 1` row is represented as `Input Stereo` and
  `Output Auto` in the new controls.
- The modern card serializes the paired contract and does not emit a new
  `StereoInput` flag. The frozen heritage row retains its existing behavior.

## Spatial and accessibility contract

- The bus panel is always visible directly below the plug-in identity and above
  an embedded editor or file-permission warning. It is supporting content for
  the card's main plug-in task, not a modal or nested scroll owner.
- Input precedes Output in semantic, visual, and keyboard order. Each selector
  has a visible label/buddy and an accessible name. The status follows the
  controls and uses text as well as color.
- Labels and status wrap; the selectors use layout negotiation rather than
  fixed coordinates. The 960 px gallery row must have no horizontal overflow,
  and the card must remain usable at the Editor's minimum practical width.
- English strings use `tr()` and the Korean catalog ships completed
  translations. Long/CJK text and disabled/focus states are gallery and manual
  inspection cases.

## State matrix

| State | Selector state | Required result |
| --- | --- | --- |
| no/missing library | disabled | neutral availability text; no guessed ABI |
| loaded VST3, untouched | enabled, `Auto -> Auto` | invites a contract probe without serializing a new pair |
| VST3 accepted | enabled | named accepted bus and channel counts |
| VST3 rejected | enabled | critical rejection and pass-through truth |
| loaded VST2, no pair | disabled | fixed VST2 input/output counts |
| loaded VST2, stale pair | disabled | ignored-contract warning and removal action |
| embedded panel open | disabled | close-panel prerequisite; accepted/rejected result preserved |
| disabled filter row | disabled by row | existing disabled-card presentation preserved |

## Traceability

| Clause | Owner/file | Proof path |
| --- | --- | --- |
| paired state and legacy migration | `VSTBusLayoutEditorModel` | EditorLogicTests |
| actual ABI gates selectors | `VSTCardEditor`, `VSTPluginLibrary` | real TestVst2/TestVst3 plug-ins |
| accepted/rejected metadata | `VSTPluginInstance`, `VSTCardEditor` | HybridConvTests + gallery diagnostics |
| serialization preserves plug-in state | `VSTCardEditor::store` | Editor `--selftest-vst` + HybridConvTests |
| five-skin layout and states | `SkinGallery` | post-change offscreen gallery and contact sheets; native capture is not claimed |
| picker capability exposure | `FilterCommandCatalog` | EditorLogicTests + picker gallery |

## Risks and limits

- The Editor probe is a real instance and the engine uses the same host
  negotiation, but another process may still observe a plug-in-specific runtime
  difference. The UI therefore says `Accepted` rather than promising ongoing
  audio success.
- Accessibility-tree inspection is required before claiming screen-reader
  proof. Label buddies, names, state text, keyboard traversal, and focus are
  implemented and behavior-checked independently.
- Packaging and installer lifecycle are adjacent but unchanged; build, launch,
  and the affected card journey are the selected desktop regression floor.
