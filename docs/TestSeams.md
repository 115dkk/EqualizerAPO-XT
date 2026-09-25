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
  lives in `Tests/Tests.props`.
- **Widget-level behavior** (anything that genuinely needs moc or a live
  QApplication) is exercised by the Editor's own offscreen gates
  (`SkinGallery` and the `--*-test` flags), not by these suites.

## Shared fixtures

Each of these replaced private copies in the suites (audit #275 D5/TD-23
created them, audit #348 D4/TD-72 moved the copies onto them). A suite that
needs one of these jobs uses the header rather than writing its own.

| Header | Job | Used by |
|--------|-----|---------|
| `Tests/TestHarness.h` | Assertions. `expectNear` compares with an absolute tolerance; `test::nearlyEqual` is the same comparison as a bool, for a check that folds several values (`a && b`, a loop verdict) into one. | every suite |
| `Tests/TestDirectory.h` | One temporary directory per suite and process, deleting the files it tracked. | EngineOrchestrationTests (one directory for the whole executable), AsioTests, and in HybridConvTests: HybridConvTests, MultiConvolutionTests, ConfigPathPolicyTests, ChannelCommandTests |
| `Tests/WavFixtures.h` | Writes a double-precision WAV, mono, interleaved or per channel. Returns false when the file does not open or a frame is not written; the caller fails the test (`require`). | HybridConvTests, MultiConvolutionTests, EngineOrchestrationTests, AudioRegressionTests |
| `Tests/Vst3Bundle.h` | Wraps a staged VST3 module in a `.vst3` bundle (`Contents\<arch>-win\`). | Vst3HostTests, SubwooferRoutingVst3Tests |

SubwooferRoutingEngineTests keeps its own directory fixture: it creates a
fresh directory per fixture instance with a collision retry and checks both
steps, which `TestDirectory` does not do.

## EngineOrchestrationTests is one executable of topic files

All sources share one `test::Harness`. `EngineOrchestrationTestSupport.h`
declares the shared fixtures (the temporary directory, the one `writeConfig`,
`testEngineSetup`/`initializeEngine`, `processDcBlock`) and every test
function; `EngineOrchestrationTests.cpp` defines the fixtures and holds
`main()` with the one call list, in a fixed order. The tests live by topic:

| File | Topic |
|------|-------|
| `RuntimeUtilityTests.cpp` | COM boundary, log destinations, registry export header, SynchronizedState, ParallelExecutor, WeakValueCache |
| `JudgedPathTests.cpp` | judged paths on a real file system (pins, attributes-only ancestors, refusals) |
| `DeviceVocabularyTests.cpp` | install value names, device test pipe name, install-state comparison, Voicemeeter strips, process search |
| `EngineLifecycleTests.cpp` | process() without a configuration, first-load publication, swap channel, channel expansion, crossfade, failed reload, config watcher |
| `EngineRoutingTests.cpp` | Channel, Copy, MultiConvolution mapping, real BRIR crossfeed |
| `ConfigLoadTests.cpp` | load trace, parse and setup errors, Include, network paths, registry port, analysis mode |

The older files (`CaptureEngineTests.cpp`, `DeviceApoInfoTests.cpp`,
`RegistryConformanceTests.cpp` and the rest) keep a `runXxxTests` runner each.
A new test is declared in the support header and called from `main()`;
`.github/scripts/Test-SourceSync.ps1` fails the build when a
`void test...(test::Harness& ...)` function in this folder is never called.
