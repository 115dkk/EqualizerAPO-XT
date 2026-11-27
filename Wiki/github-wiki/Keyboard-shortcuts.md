# Keyboard shortcuts

Every key and mouse gesture the Configuration Editor understands, gathered in one place.

The most useful ones:

* **Shift while dragging a knob** turns it at a tenth of the normal speed, for fine adjustment.
* **Shift while dragging a value box** moves it four times more finely.
* **Double-click a knob** on the Preamp, Delay and All-pass cards puts it back to its default.
* **Ctrl+Z / Ctrl+Y** undo and redo changes to the filter list.

## Menus

| Key | Menu item | What it does |
|---|---|---|
| Ctrl+N | File > New | Opens a new configuration |
| Ctrl+O | File > Open | Opens a configuration file |
| Ctrl+S | File > Save | Saves the active tab |
| Alt+F4 | File > Exit | Closes the Editor |
| Ctrl+Z | Edit > Undo | Undoes the last change to the filter list |
| Ctrl+Y | Edit > Redo | Redoes it (Ctrl+Shift+Z is not used) |
| Ctrl+X | Edit > Cut | Cuts the selected lines |
| Ctrl+C | Edit > Copy | Copies the selected lines as text |
| Ctrl+V | Edit > Paste | Pastes lines into the filter list |
| Ctrl+A | Edit > Select all | Selects every line |
| Ctrl+Alt+1 to Ctrl+Alt+5 | View > Interface > Modern cards or Modern rows | Switches skin within the current presentation: 1 Studio Glass, 2 Precision Minimal, 3 Stock Flat, 4 Hardware Rack, 5 Signal Matrix. In Modern rows, a skin that does not draw rows opens as Modern cards |
| Ctrl+Alt+D | View > Interface > Dark theme | Switches between the light and dark theme |
| Ctrl+Alt+F | View > Interface > Fullscreen graph | Lets the analysis graph fill the window, hiding the filter list and toolbar |

The skin and theme shortcuts do nothing in the **Legacy editor**. Modern rows take the same keys and gestures as the cards everywhere on this page; where it says cards, it means both, and the header of a row is its left column. While a text field has the focus, Ctrl+Z, Ctrl+Y, Ctrl+X, Ctrl+C, Ctrl+V and Ctrl+A act on the text in that field, not on the filter list.

After you remove lines with a remove button or the Delete key, a notice with an **Undo** button stays up for 8 seconds. When you close a tab or the window with unsaved changes, Enter saves and Esc cancels.

## Filter list

These work when the filter list has the focus, in both the cards and the Legacy editor.

| Key or gesture | What it does |
|---|---|
| Up / Down | Moves to the previous or next line and selects only that line |
| Shift+Up / Shift+Down | Extends the selection |
| Ctrl+Up / Ctrl+Down | Moves the focus without changing the selection |
| Space | Selects the focused line |
| Ctrl+Space | Adds the focused line to the selection or takes it out |
| F2 | Edits the focused line as raw text |
| Delete | Removes the selected lines |
| Click | Selects that line |
| Ctrl+click | Adds a line to the selection or takes it out |
| Shift+click | Selects every line from the last selected one to this one |
| Drag a line by its header | Moves the selected lines; hold **Ctrl** when you drop to copy them instead |

Clicking a line that is already part of a selection keeps the selection, so the whole group can be dragged. Dropping lines on another tab moves them there.

When you edit a line as raw text, Enter or clicking elsewhere applies the text. In the Legacy editor, Esc cancels and restores the old text; in the cards, Esc does not cancel.

The mouse wheel over the list scrolls the list. A knob, value box or drop-down list in a card only takes the wheel after you click it.

**Add filter** at the bottom of the list and **Insert filter at the top** open the filter picker on Enter or Space as well as on a click.

## Knobs

Knobs appear on the Preamp, Delay and All-pass cards and, in the Legacy editor, on the editors for filters, delay and loudness correction.

| Gesture | What it does |
|---|---|
| Drag | Turns the knob. In Studio Glass, Stock Flat, Hardware Rack, Signal Matrix and the Legacy editor the knob follows the pointer around its arc; in Precision Minimal you drag up and down, and 200 pixels cover the whole range |
| **Shift** while dragging | Turns it at a tenth of the speed |
| Double-click | Resets it: Preamp to 0 dB, Delay to 0, All-pass to 100 Hz and its default Q |
| Up / Down or Left / Right | One step (0.1 dB on the Preamp card) |
| Page Up / Page Down | Ten steps |
| Home / End | Minimum / maximum |
| Wheel | Turns it after you have clicked the knob |

Double-click reset works on the Preamp, Delay and All-pass cards only. The knobs in the Legacy editor have no reset.

Double-clicking the number under a knob opens it for typing. Enter or clicking elsewhere applies what you typed.

## Value boxes

Value boxes are the number fields without arrows: the Graphic EQ card's frequency and gain, the IIR and Velvet cards, and the number fields in the Legacy editor.

| Gesture | What it does |
|---|---|
| Drag up / down on the number | Changes it by one step about every 4 pixels; dragging sideways selects the text instead |
| **Shift** while dragging | One step every 16 pixels, four times finer. You can press or let go of Shift in the middle of a drag |
| Up / Down | One step |
| Page Up / Page Down | Ten steps |
| Wheel | Changes it after you have clicked the box; Ctrl with the wheel moves ten steps |

## Graphic EQ

On the Graphic EQ card's plot:

| Key or gesture | What it does |
|---|---|
| Click a point | Selects it; Ctrl+click adds it to the selection or takes it out |
| Drag a point | Moves it |
| Up / Down | Raises or lowers the focused point by 1 dB |
| Left / Right | Moves the focused point by 1 Hz (variable band layout only) |
| Delete / Backspace | Removes the selected points (variable band layout only) |
| Double-click on an empty spot | Adds a point (variable band layout only) |
| Wheel | Zooms the dB axis around the pointer |
| Right-drag | Moves the dB axis up and down |

The arrow keys move only the focused point, and there is no Shift fine step on this plot. In the Legacy editor the arrow keys move every selected point, the wheel zooms both axes (or only one when the pointer is over that axis's ruler), and Backspace does not remove points.

## Other cards

* **Switches** with several options (All-pass order, Hilbert direction, Velvet mode, the analysis metric): Left / Up for the previous option, Right / Down for the next, Home and End for the first and last.
* **Channel and NewChannel cards:** type a name in the add-channel field and press Enter.
* **Subwoofer routing card:** double-click or press Enter to open the routing editor.
* **Include and VST cards:** click the file or plug-in name to open the file or the plug-in panel.
* **VST card:** the bus layout selectors and the slot channel cells open their menu with Space, Enter or Down; the channel fill switch toggles with Space or Enter.

## Copy and channel routing

How the routing view of the Copy card reacts depends on the skin.

* **Studio Glass:** click a line to select it, Ctrl+click to add or remove it. Drag from one channel to another to connect them. Click a channel to select all its lines. Double-click a line to change its factor. Delete or Backspace removes the selected lines.
* **Hardware Rack and Signal Matrix:** click a crossing point to connect or disconnect at unity gain; double-click to type a factor.
* **Precision Minimal and Stock Flat:** double-click a term to edit it.
* **Legacy editor:** Delete removes the selected connections; double-click a connection to edit its factor, and Esc cancels the edit.

## Filter picker

The picker opened by Add filter or a card's **+** button closes with Esc or a click outside it.

* **Studio Glass, Stock Flat, Hardware Rack and the default picker:** type to search; Up / Down and Page Up / Page Down move through the list, Enter picks.
* **Precision Minimal:** Up / Down move one entry, Page Up / Page Down ten, Enter picks.
* **Signal Matrix:** type to search and Backspace to delete a character; Up / Down move, Left / Right and Page Up / Page Down switch group, Home / End jump to the first and last entry, Enter picks.

## Window

With the Editor's own title bar (not the native one), double-clicking the title maximizes or restores the window. Dialogs use Enter for OK and Esc for Cancel.

## Device Selector

In the Device Selector, the **Troubleshooting options** header opens and closes with Space or Enter (reach it with Tab). Right-click a device to copy its `Device:` command. Enter is OK and Esc is Cancel.

## See also

* [Documentation](Documentation): the Editor section describes the cards and skins.
* [Korean version of this page](Korean-Keyboard-shortcuts)
