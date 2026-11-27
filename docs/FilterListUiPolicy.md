# Filter List UI Policy

The Editor's filter list can render in three modes, selected at runtime through
`FilterTable::RenderMode`. This document records which path is canonical, why
the legacy one is kept rather than deleted, and the rule contributors follow so
the same list behavior is not implemented twice.

## The three render modes

`FilterTable::RenderMode` (declared in `Editor/FilterTable.h`) has three values:

| Mode | Row widget | Status | Representative files |
| --- | --- | --- | --- |
| `ModernCards` | `FilterCardRow` | Canonical, actively maintained | `Editor/widgets/FilterCardRow.cpp`, `Editor/widgets/FilterCardModel.cpp`, `Editor/widgets/cards/` |
| `ModernRows` | `FilterCardRow` with `inlineRow` | Canonical: the card path laid out as one row per line, and what a new installation starts in. Every skin draws it (`ISkin::supportsRowMode`); under a skin that answered false the table would build cards | `Editor/widgets/FilterCardRow.cpp`, each skin's `*Skin.CommandRows.cpp` |
| `LegacyEditor` | `FilterTableRow` | Kept; guarantees only that existing usage keeps working | `Editor/FilterTableRow.cpp` |

`FilterTable` branches between the card path and the legacy path when it builds
row widgets. The two branch points are in `FilterTable::updateGuis()` (full
rebuild) and `FilterTable::updateSingleRowGui()` (single in-place row refresh),
both in `Editor/FilterTable.cpp`. Cards and rows are not a branch of their own
there: `FilterTable::effectiveRenderMode()` decides which of the two the card
path lays out, and falls back to cards under a skin without rows (every skin
draws rows today, and the CI row runs fail on a fallback). A table that is told
nothing builds cards (the `renderMode = ModernCards` member initializer in
`Editor/FilterTable.h`); the headless checks rely on that and name rows with
`--list-mode rows` when they want them.

The user's choice is stored as one word in `interface/listMode` (`cards`,
`rows` or `legacy`; `services/settings/EditorSettings.h`) and made in View >
Interface, where Modern cards and Modern rows each list the skins that draw
them and Legacy editor is an entry of its own. A machine with no stored word
gets one at the Editor's first interactive start (`EditorSettings::adoptListMode`,
called from `Editor/main.cpp`): `rows` for a new installation, `cards` for a
store that already holds interface values from a build older than the word.
The store's existence is not the sign, because the original Equalizer APO's
editor keeps its geometry under the same registry key.

## Why the card path is canonical

`ModernCards` is the UI shipped to users: it is the runtime default, it owns the
per-row enable/disable affordance in the card header, it renders the modern
routing and knob editors, and it carries the depth/indentation handling for
nested includes. New filter-list work has been going into the card path, so it is
where the current behavior lives and where the maintained code is.

## Rows are the card path, not a third implementation

A Modern row is built from the same widgets, editors and decisions as a card
(`FilterCardRow`, the editors in `Editor/widgets/cards/`, the skin's
`prepareCommandRow` hook). Only the layout differs: the header's parts stand in
a column at the row's left and the editor beside them, with no header strip
above a body. A list is never a mixture of the two. What follows from that:

- No behavior exists in rows only or in cards only. A fix made for one is a fix
  of the shared code and is checked in both.
- The headless checks of the list run in both presentations (`--list-mode
  rows`), and CI runs each of them twice. A row run must report at least one
  skin that drew rows, or it fails; `docs/skin-integration-report.md` has the
  options.
- What a skin has to supply for rows is in `docs/skin-hooks.md` ("Modern
  rows").

## Why legacy is kept rather than deleted

`LegacyEditor` (`FilterTableRow`) is the original Qt-table-style row UI, kept
permanently by maintainer decision (2026-07-05): it is the heritage editor,
preserved as the unmodernized original design, not a deprecation candidate.

## The heritage presentation

The Legacy editor (called Legacy rows up to v2.85) is a whole presentation,
not just a different row widget. When `interface/listMode` is `legacy` the
Editor starts with the platform's native
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
  supporting `Editor/widgets/` code), where cards and rows both get it.
- The Legacy editor guarantees only that existing usage keeps working
  (maintainer decision, audit #348 B1). It gets no new presentation of its own, and
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
row fills its bus slots from the selection. `FilterCardModel::prepareDocument`
builds the flow lines, calls `computeChannelFlow`
(`Editor/widgets/ChannelFlow.h`) once, and returns both the card scopes and the
per-line flow. `FilterTable` builds or repositions cards from those scopes and
hands each row widget its own flow element through `IFilterGUI::setChannelFlow`.
Rows only read that element; no row changes what the rows below it see. All
three render modes use the same flow. A card's inherited channel badges come from the
selection in this result, while a Channel card's own badges show that line's
selectors through `ChannelCommand`.

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
