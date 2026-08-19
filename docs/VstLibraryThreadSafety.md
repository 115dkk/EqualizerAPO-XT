# VST Plugin Library Thread Safety

`VSTPluginLibrary` (and its base `AbstractLibrary`) are shared between two threads
in the Editor and were not synchronized, which crashed the Editor when a config
used VST plugins.

## The race

- The Editor GUI thread creates and destroys VST card editors
  (`Editor/widgets/cards/VSTCardEditor.cpp`) whenever the filter rows are rebuilt
  — for example on every skin switch or dark-theme toggle, which call
  `FilterTable::updateGuis()`.
- `MainWindow`'s background `AnalysisThread` (`Editor/AnalysisThread.cpp`) builds
  its own `FilterEngine` for the analysis graph. That engine resolves the same
  VST plugins through `VSTPluginFilterFactory`.

Both paths funnel into `VSTPluginLibrary::getInstance()`, which read/modified the
static `instanceMap` (a plain `std::unordered_map`) with no lock. Concurrent
`find`/`insert` from the two threads corrupted the map and crashed. Two more
shared mutables had the same problem:

- `VSTPluginLibrary::getDefaultPluginPath()` lazily assigns the static
  `defaultPluginPath` string. The factory calls it for relative library paths, so
  it runs on both threads too.
- A single library instance is shared by both threads (same path → same object),
  and both call `AbstractLibrary::initialize()`, which lazily `LoadLibrary`s and
  writes the `module` handle.

This is independent of layout ordering: it reproduced with both the old
(`applySkin` → `updateGuis`) and the new (`clearRows` → `applySkin` →
`updateGuis`) skin-switch sequence.

## The fix

- `getInstance()` takes a static mutex around the whole `instanceMap` lookup/insert.
- `getDefaultPluginPath()` takes a static mutex around its lazy assignment.
- `AbstractLibrary::initialize()` takes a per-instance mutex around the lazy module
  load, so two threads sharing one instance cannot race on `module`/`LoadLibrary`.

`shared_ptr` reference counting is already atomic, and the OS `LoadLibrary`/
`FreeLibrary` calls are themselves thread-safe (the loader lock serializes them),
so once the map, the lazy statics, and the lazy module load are guarded, the
load/unload path is safe.

## The second scheme: per-instance VST3 synchronization

The mutexes above guard library load/unload. A `VSTPluginInstance` hosting a
VST3 plugin carries a second, independent synchronization scheme of its own
(audit #275 TD-35; it previously existed only as code comments in
`vst/VSTPluginInstance.cpp`, around lines 206-344):

- **`vst3LifecycleMutex`** serializes the component lifecycle transitions
  (activate/deactivate, processing start/stop) against the control thread.
  Process calls themselves do not take it - the audio thread must not block
  on a mutex the GUI thread can hold.
- **`vst3Processing` (atomic)** tells the control thread whether the audio
  side currently owns the component state. While it is set, a parameter edit
  must not be persisted synchronously: the component only adopts the value
  when its next process call drains `inputParameterChanges`, so a synchronous
  save would persist the previous state.
- **The SPSC parameter-edit ring** (`vst3ParameterEdit{Write,Read}` atomics
  over a 1024-slot ring) carries parameter edits from the single control
  thread (the Editor GUI thread for `performEdit`/`writeToEffect`; in the
  engine, the configuration loader before processing starts) to whichever
  thread runs the next process call. A full ring drops the edit - the next
  edit of the same control supersedes it anyway. The single-producer
  assumption is a hosting contract: nothing may queue edits from two threads
  at once.

When touching this area keep the two schemes distinct: the library mutexes
protect *which modules exist*, the instance scheme protects *one component's
state hand-off between the control and audio threads*.

## Manual repro / verification

Automated testing is impractical: the crash is a nondeterministic data race that
needs two real OS threads loading the same plugin, and `VSTPluginLibrary` pulls in
the VST3 SDK plus the registry/log helpers. Verify by hand:

1. Put a real VST2/VST3 plugin reference in the active config (`config.txt`), e.g.
   `VSTPlugin: Library SomeReverb.dll`.
2. Open the Editor and make sure the analysis graph dock is visible (so the
   `AnalysisThread` is loading the same plugin in the background).
3. Repeatedly switch skins and toggle the dark theme (`Ctrl+Alt+1..5`,
   `Ctrl+Alt+D`). Before the fix this crashed within a few switches while the
   analysis was running; after the fix it stays up.

CI builds `Common` (which contains both changed files) for every SIMD/arch
variant, so compilation of the change is covered by the normal build matrix.
