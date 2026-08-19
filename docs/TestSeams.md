# Test seams: what each suite can and cannot compile

Written for audit #275 (TD-30/B3, TD-36): two pieces of load-bearing
knowledge about the test projects lived only in project-file comments, and
one of them had already been violated once at the cost of the largest
untested UI model in the tree.

## EditorLogicTests compiles a widget-free subset, without moc

`Tests/EditorLogicTests` is the seam for Editor logic: document and selection
state live in widget-free models so this suite can verify them as a plain
console binary. Two constraints define which sources may join its list
(`EditorLogicTests.vcxproj`):

1. **No Qt widget stack.** The binary links Qt6Core/Gui/Widgets import
   libraries but constructs no QApplication; sources must stand without the
   widget machinery.
2. **No moc.** The project has no moc step, so any source that needs
   generated meta-object code - `Q_OBJECT`, signals, slots - cannot be
   compiled here. `Q_DECLARE_TR_FUNCTIONS` is fine (it needs no moc), which
   is how `FilterCardModel` translates.
3. **No engine sources.** The suite links `Common.lib` whole-archive for the
   filter factories; adding an engine `.cpp` to the source list would define
   the same symbols twice and break the link.

The consequence of (2) is a design convention, not just a build detail:

> **A model that wants EditorLogicTests coverage keeps its behavior in a
> signal-free core.** A `Q_OBJECT` model silently falls outside the seam -
> the build does not fail, the tests just cannot reach it.

Precedents: `FilterListModel` (signal-free from the start) and
`SubwooferRoutingUiState` (the mutation/validation core extracted from the
`SubwooferRoutingUiModel` QObject shell, whose only remaining job is turning
"this mutated" into the two signals widgets connect to).

## The other suites

- **HybridConvTests / EngineOrchestrationTests / AudioRegressionTests** are
  Qt-free consoles linking `Common.lib` whole-archive; shared scaffolding
  lives in `Tests/Tests.props`, shared fixtures in `Tests/TestHarness.h`,
  `Tests/TestDirectory.h` and `Tests/WavFixtures.h`.
- **Widget-level behavior** (anything that genuinely needs moc or a live
  QApplication) is exercised by the Editor's own offscreen gates
  (`SkinGallery` and the `--*-test` flags), not by these suites.
