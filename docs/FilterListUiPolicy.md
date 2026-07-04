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
| `LegacyRows` | `FilterTableRow` | Frozen fallback, not extended | `Editor/FilterTableRow.cpp` |

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

## Why legacy is frozen rather than deleted

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

- Add new filter-list behavior only to the card path (`FilterCardRow` and the
  supporting `Editor/widgets/` code).
- Do not extend `FilterTableRow`. Treat it as frozen. Bug-for-bug parity with
  the card path is not a goal.
- Do not change the runtime default or remove either branch as part of unrelated
  work. The freeze is a hold, not a deprecation schedule; any decision to delete
  the legacy path is a separate, explicit change.
- Document-level features that live on `FilterTable` itself (above the row
  widgets) are not an extension of the legacy path and apply to both modes.
  Undo/redo is the existing example: `FilterListUndo` snapshots the config
  lines on every `linesChanged` and replays them through the same full-rebuild
  path as a document load, so it needs nothing row-specific.
