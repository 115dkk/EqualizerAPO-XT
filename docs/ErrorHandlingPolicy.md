# Error Handling Policy

EqualizerAPO-XT uses different error-reporting models in different layers.
The split is intentional: each layer picks the model that matches when it runs
and what the caller can do about a failure. This document records the per-layer
rule so callers know what to expect and reviewers do not "fix" one model into
another by accident.

## Per-layer models

| Layer | Model | Representative files |
| --- | --- | --- |
| System integration (registry, service control) | Throw an exception | `helpers/RegistryHelper.cpp`, `helpers/ServiceHelper.cpp` |
| Install / uninstall orchestration | Return a `Result` enum | `helpers/ApoRegistration.h`, `helpers/ApoRegistration.cpp` |
| VST plugin loading | Return `bool` | `vst/VSTPluginInstance.cpp` |
| Allocation helpers (audio / real-time path) | Return `nullptr` and log | `helpers/MemoryHelper.cpp` |
| Configuration parsing | Report the line and keep loading | `filters/*Factory.cpp`, `ConfigLoadTrace.h` |

## Why each layer differs

### System-integration / setup-time code throws

`RegistryHelper` and `ServiceHelper` throw on failure. `RegistryHelper::readValue`
and the other registry accessors throw `RegistryException` (declared in
`RegistryHelper.h`) with a descriptive message; `ServiceHelper::restartService`
and `Service::fail` throw `ServiceException` (declared in `ServiceHelper.h`).

These routines run once, off the audio path, during install, uninstall, or a
service restart. A registry value that is missing or has the wrong type, or a
service that will not stop, has to abort the whole operation. An exception
carries a human-readable message straight to the setup or Editor UI and unwinds
the multi-step sequence without every caller threading a status code back up by
hand. The cost of throwing does not matter here because nothing real-time is
running.

### Install/uninstall orchestration returns a `Result` enum

`ApoRegistration::install` and `ApoRegistration::uninstall` return
`ApoRegistration::Result` (`Success`, `RegistrationFailed`, `RegistryFailed`,
`AclFailed`, `ServiceFailed`, `DeviceUninstallFailed`, `DllNotFound`). The
orchestration runs a fixed sequence of steps and the caller needs to know which
step failed so it can report the right message and decide whether to continue,
roll back, or stop. A named enum makes that branch explicit at the call site
instead of wrapping the whole sequence in a `catch` that has lost track of which
step threw.

Auxiliary entry points on the same class do not use the enum because they do not
need it: simple yes/no helpers such as `stopAudioService` and
`createStartMenuShortcuts` return `bool`, and `registerComServer` /
`waitForProcess` return an `int` so the caller sees the real COM `HRESULT` or
process exit code. The `Result` enum is the model for the staged install/uninstall
flow specifically.

### VST plugin loading returns `bool`

`VSTPluginInstance::initialize` (and the internal `initializeVST2` /
`initializeVST3`) return `bool`. Loading a third-party plugin is runtime work
that can fail in ordinary ways (wrong magic number, a VST3 interface that will
not instantiate) or crash outright; `initializeVST2` even wraps the plugin entry
point in a structured-exception guard and reports the crash as `false`. A plain
`bool` keeps that failure local: the engine skips the plugin and keeps building
the rest of the filter graph, with no exception crossing the graph-construction
path. The caller only needs "did it load," not a failure taxonomy.

### Configuration parsing reports per line and never stops the load

A configuration file is the one untrusted text this program reads, and it is
edited by hand. Half of one is still worth running: a broken `Convolution:` line
must not take the twelve working filters below it with it, because the user would
then have no sound at all instead of sound without one effect.

So a factory that recognises its command and cannot use the parameters calls
`ParseReportingFactory::reportParseError(command, reason)`. That logs the file and
line, and hands the same reason to `ConfigLoadTrace` so the Editor can mark the
row. The load continues with the next line. Nothing throws: `createFilter` runs
inside the engine's dispatch loop for every line of every file, and an exception
there would abandon the rest of the configuration.

Two rules make this honest. **The factory reports, not the engine.** The engine
cannot tell a broken parameter list from a command that legitimately produces no
filter - `Preamp: 0 dB` produces none, and so does every control-flow command -
and it used to guess from the outside with a hand-maintained list of commands to
excuse, which meant a new no-op filter became a false warning until somebody
remembered the list. **A line no factory claimed is not an error.** Prose,
comments and unknown keys are how 1.4.2 configurations carry notes, and reporting
them would bury the real diagnostics.

### Allocation helpers return `nullptr` by contract

`MemoryHelper::alloc` returns `nullptr` on failure and logs the requested size
with `LogFStatic`. It does not throw and it does not abort. This is the contract
on the audio / real-time path: that code must not throw, so a null pointer is the
lightweight no-throw failure signal. Callers are responsible for checking the
return value before using the buffer. The general rule for the real-time and
audio-processing path is status returns and no-throw behavior; `MemoryHelper` is
the representative allocation case.

## Note on convergence

Converging these five models into one is out of scope for this change and is
intentionally deferred. The models are kept distinct on purpose: the boundary
runs along "setup-time, failure aborts the operation" (exceptions or a staged
`Result`) versus "real-time / audio path, failure must stay local and
non-throwing" (`bool` or `nullptr`). Any future unification should preserve that
boundary rather than force the real-time path to adopt setup-time error handling.
