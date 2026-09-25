# Filter List UI Policy

The Editor's filter list can render in two modes, selected at runtime through
`FilterTable::RenderMode`. This document records which mode is canonical, why the
other is kept rather than deleted, and the rule contributors follow so the same
list behavior is not implemented twice.

## The two render modes

`FilterTable::RenderMode` (declared in `Editor/FilterTable.h`) has two values:

| Mode | Row widget | Status | Representative files |
| --- | --- | --- | --- |
| `ModernCards` | `FilterCardRow` | Canonical, actively maintained, runtime default | `Editor/widgets/FilterCardRow.cpp`, `Editor/widgets/FilterCardModel.cpp`, `Editor/widgets/cards/` |
| `LegacyRows` | `FilterTableRow` | Kept; guarantees only that existing usage keeps working | `Editor/FilterTableRow.cpp` |

`FilterTable` branches on the active mode when it builds row widgets. The two
branch points are in `FilterTable::updateGuis()` (full rebuild) and
`FilterTable::updateSingleRowGui()` (single in-place row refresh), both in
`Editor/FilterTable.cpp`. The default is set by the `renderMode = ModernCards`
member initializer in `Editor/FilterTable.h`.

## Why cards is canonical

`ModernCards` is the UI shipped to users: it is the runtime default, it owns the
per-row enable/disable affordance in the card header, it renders the modern
routing and knob editors, and it carries the depth/indentation handling for
nested includes. New filter-list work has been going into the card path, so it is
where the current behavior lives and where the maintained code is.

## Why legacy is kept rather than deleted

`LegacyRows` (`FilterTableRow`) is the original Qt-table-style row UI, kept
permanently by maintainer decision (2026-07-05): it is the heritage editor,
preserved as the unmodernized original design, not a deprecation candidate.

## The heritage presentation

Legacy rows are a whole presentation, not just a different row widget. When
`interface/legacyRows` is set the Editor starts with the platform's native
widget style (no Fusion/CustomStyle), no skin stylesheet or palette
(`SkinManager::applyHeritage`), the stock ClearType font engine and system
fonts (no bundled DM Sans/Pretendard), the native Windows caption (no custom
TitleBar), the classic cascading add menu, and the classic `CopyFilterGUI`
node scene (no skin routing renderer). Skin and dark-theme menu items are
disabled while it is active. Switching between the modes restarts the Editor:
none of those pieces can swap cleanly in a live process, and a partial swap is
exactly the modern-chrome-around-legacy-rows mixture this mode must not show.

The offscreen gallery renders the heritage presentation with
`EAPO_GALLERY_LEGACY=1` (two whole-table dumps) for eyeball regression checks.

## The rule for contributors

The maintenance concern this policy addresses is that, with two parallel UIs,
every list behavior risks being implemented twice. The rule is therefore:

- Add new filter-list behavior to the card path (`FilterCardRow` and the
  supporting `Editor/widgets/` code).
- Legacy rows guarantee only that existing usage keeps working (maintainer
  decision, audit #348 B1). They get no new presentation of their own, and
  parity with the card path is not a goal in itself.
- Logic a legacy row shares with its card lives in one place that both use,
  never in two copies. The legacy VST row shares its document model
  (`VSTRowDocument`) and its plugin session (`VSTPluginSession`) with the card,
  so a VST feature lands in both. The channel flow (below) is shared by every
  row.
- Do not change the runtime default or remove either branch as part of unrelated
  work. Keeping the legacy path is not a deprecation schedule; any decision to
  delete it is a separate, explicit change.
- Document-level features that live on `FilterTable` itself (above the row
  widgets) are not an extension of the legacy path and apply to both modes.
  Undo/redo is the existing example: `FilterListUndo` snapshots the config
  lines on every `linesChanged` and replays them through the same full-rebuild
  path as a document load, so it needs nothing row-specific.

## The channel flow

Several rows show which channels exist and which are selected at their line: a
Channel row offers the names in scope, a Copy row routes between them, a VST
row fills its bus slots from the selection. `FilterTable::propagateChannels`
builds the lines from the document, calls `computeChannelFlow`
(`Editor/widgets/ChannelFlow.h`) once, and hands each row widget its own line's
element through `IFilterGUI::setChannelFlow`. Rows only read that element; no
row changes what the rows below it see. Both render modes use the same flow.

The flow follows the engine: `Channel:` replaces the selection
(`ChannelCommand::resolveSelection`), `Copy:` adds its targets to the names in
scope (`propagateCopyChannels`), a switched-off line changes nothing, and lines
the engine skips because a `Device:` pattern or a `Stage:` does not match the
selected device change nothing (`DeviceCommand::matches`,
`StageCommand::matches`). The Editor judges `Stage:` as the post-mix instance
with a post-mix APO installed, like its analysis engine.

Two things the Editor cannot know, so the flow approximates them:

- `If:`/`ElseIf:`/`Else:`/`EndIf:`: which branch runs depends on expressions
  over the live device. Each branch starts from the state at its `If:` line, and
  after `EndIf:` the flow continues with the state the `If:` branch left, as if
  the first condition held.
- A line whose parameters carry an inline `` `expression` `` changes nothing,
  because its values exist only after the engine evaluates them. `Include:`
  changes nothing either: the engine restores the selection after the included
  file, and the names a `Copy:` inside it creates are not visible to the Editor,
  which does not read the included file.
