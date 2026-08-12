# VST card presentation contract

## User outcome

The VST card presents one editable plug-in slot. Its resting state communicates
the selected plug-in, the requested input/output layout, and whether that
layout is active. It does not present the backing configuration syntax as UI.

## Visible hierarchy

1. Card header: `VST Plugin`, plug-in filename, and requested bus pair.
2. Plug-in row: product name, VST generation, and the existing browse/panel/edit
   actions.
3. Main bus: paired input and output selectors.
4. One short status line: active, unavailable, panel-locked, detected, or VST2
   fixed-bus state.

## Source and path policy

- Serialized command text is shown only when the row has no structured editor
  or routing view.
- A structured VST card never shows `Raw`, `VSTPlugin:`, `Library`, an absolute
  filesystem path, or an `ABS` badge in its resting body.
- Expert source/path editing remains reachable through the existing edit
  action; information is moved out of the resting presentation, not deleted.

## State policy

- Accepted VST3: green `Active` line with active layout and channel counts.
- Rejected VST3: red unavailable/pass-through line.
- VST2 with saved VST3 keys: amber fixed-bus notice and a repair action labeled
  `Remove saved layouts`.
- Embedded panel open: selectors are disabled and the status explains why.

## Accessibility and localization

Existing keyboard focus, tooltips, mnemonics, and control semantics are
unchanged. New visible strings are translated through Qt; Korean is completed
in the generated catalog. Native screen-reader output was not exercised in the
offscreen gallery and remains outside this correction's verified claims.
