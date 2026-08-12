# Frontend run receipt

Status: passed

Run: `20260811T233023Z-editor-graph-layout-hotfix`

Surface: EqualizerAPO-XT Qt Widgets Editor on native Windows

Owner and claims: `native`; interaction, visual, and target behavior

## Outcome contract

- Affected users — people editing filters with the analysis graph docked on the right, and people changing the active card with a mouse. Evidence: issue description plus deterministic reproduction. Confidence: high.
- Job — keep filter cards usable beside the graph, make the graph's header/control cell occupy its actual dock, and make a plain click leave exactly one coherent selected/focused card. Evidence: before logs and captures. Confidence: high.
- Outcome — right-dock startup is bounded and proportional; bottom layout remains compact; ordinary and control-consuming clicks synchronize model and chrome across all five skins. Evidence: candidate logs, native captures, and regression tests. Confidence: high.
- Assumption — multi-selection remains intentional for Ctrl/Shift and drag workflows; only an unmodified click collapses it. Supported by existing model code and card-move behavior. Confidence: high.
- Unknown — behavior on DPI/theme/locale targets outside the disclosed matrix. Confidence: not claimed.

## Implementation decisions

- Existing `.ui`, skin tokens, and skin-owned card chrome remain the design authority; no new visual token or competing design source was added.
- The graph remains horizontal. A bounded 320-480 logical-pixel starting dock, capped to protect the card workspace, fixes the reported compression without rotating the frequency axis.
- Right-dock controls use an expanding width policy; top/bottom positions retain the existing 250px cap.
- A card now explicitly synchronizes cached `CommandRowInfo` after selection/analysis mutations. Plain clicks on editor controls are observed without consuming the control's event; dynamically added body widgets inherit the same observation.
- Qt Editor exposure only; no configuration grammar, engine, installer, translation, or packaging surface changed.

## Verification

| Gate | Result | Evidence |
| --- | --- | --- |
| Release x64 AVX2 Qt build | Passed | `app-automation-transcript.txt` |
| Analysis Right -> Bottom -> Right, native | Passed | `after-layout-test-native.log`, `after-right-native.png` |
| Analysis Right -> Bottom -> Right, offscreen | Passed | `after-layout-test-offscreen.log`, `after-right-offscreen.png` |
| Card pointer/keyboard selection, native, 5 skins x 2 modes | Passed, 10/10 | `card-selection-native.log`, `card-selection-native/` |
| Card selection before/after regression, offscreen | Before failed 20 assertions; after passed 10/10 scenes | `card-selection-before.log`, `card-selection-after.log`, corresponding image folders |
| Card move regression | Passed, 20 moves, worst 103ms | `card-move-test.log` |
| Skin switch regression | Passed, 30 switches, worst 2027ms | `skin-switch-test.log` |
| Editor logic unit tests | Passed, 2952 checks | `app-automation-transcript.txt` |
| `git diff --check` | Passed | recorded in `app-automation-transcript.txt` |
| Repository static gate | CI `cppcheck` required; local binary unavailable | CI result will be the authoritative gate before merge |

## Evidence registry

- `device-report.json` — device-report
- `app-automation-transcript.txt` — app-automation-transcript
- `after-right-native.png` — screenshot, full native Editor/right-dock state
- `card-selection-native/studio_dark_card-selection.png` — screenshot, representative native card state
- `before-right.png` / `before-layout-test.log` — screenshot/log, reproduced layout failure
- `card-selection-before/` / `card-selection-before.log` — screenshots/log, reproduced stale card state
- `after-right-offscreen.png` / `after-layout-test-offscreen.log` — screenshot/log, CI-equivalent layout path
- `card-selection-after/` / `card-selection-after.log` — screenshots/log, CI-equivalent selection matrix

## Limitations

See `VISUAL_QA.md`. No accessibility, package lifecycle, or cross-platform claim is made.
