# Velopack Update Checks

EqualizerAPO-XT publishes installers through the Velopack release job in GitHub Actions. Velopack creates channel-specific assets such as `releases.x64-avx2.json`, `EqualizerAPO-XT-x64-avx2-<version>-x64-avx2-full.nupkg`, and `EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe` (the channel appears twice because Velopack appends it to a pack id that already carries it; `.github/scripts/ReleaseAssets.psm1` owns the spelling).

The Editor's in-app Velopack update (below) is the only update path. It reads the channel's feed, `releases.<channel>.json`, from the latest GitHub Release of `115dkk/EqualizerAPO-XT`. The standalone `UpdateChecker.exe`, a notify-only tool that nothing started automatically after the NSIS installer left (audit #250 F073), was removed in audit #348.

The channel names are the `Channel` values in `.github/simd-variants.psd1`:

- `x64-sse2`
- `x64-avx`
- `x64-avx2`
- `x64-avx512`
- `x64-avx10-1`
- `arm64-neon`

There is no hard-coded default update channel in `Editor/Editor.pro`: it defines `EAPO_UPDATE_CHANNEL` only when qmake receives a nonempty value. `.github/scripts/Build-QtApps.ps1` takes `-UpdateChannel`, or reads the selected variant's `Channel` when that argument is empty, and passes it to qmake as `EAPO_UPDATE_CHANNEL`. Use that script for local builds too.

APO installation and device registration run through the Velopack hooks the Editor handles (`--veloapp-install`, `--veloapp-updated`, etc.), which call `ApoRegistration`. The NSIS installer has been removed.

Device Selector saves the original driver registry values to
`%ProgramData%\EqualizerAPO-XT\Backups\backup_<device>_<connection>.reg`.
Only Administrators and SYSTEM can write the backup folders; ordinary users
can read the files for recovery. The installation report records the full path.
Older backups already in the config folder are left there, and uninstall keeps
both locations' files so they remain available for manual recovery.

## Editor in-app auto-update

The Editor embeds the native Velopack client (`velopack_libc`) and updates itself without sending the user anywhere:

1. On a normal launch the Editor calls `Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run()`. Auto-apply on startup is off because updates are applied on exit instead.
2. About 60 seconds after start (only for Velopack installs, and only while Settings > Automatic updates is checked, which is the default), a background worker checks the GitHub release feed for the build's channel with `UpdateManager::CheckForUpdates()` and, if a newer build exists, downloads it with `DownloadUpdates()` into the Velopack staging area. The download runs off the GUI thread. If the Editor closes before it finishes, the window closes and the session cancels applying that download. The process waits for the native download call to return, then exits without applying the result or requesting elevation. The C API has no cancellation operation; its `void` progress callback cannot abort the call.
3. When an update finished staging before the Editor exits, it asks for elevation once and launches a short-lived elevated Editor coordinator. The coordinator reopens the staged package with `UpdatePendingRestart()`, calls `WaitExitThenApplyUpdates(info, silent: true, restart: false)`, and exits. The updater inherits that token, waits for the coordinator to close, swaps the files silently, and does not relaunch. The new version comes up on the next launch.
4. Troubleshooting > Check for updates runs the same check and download on request, whether or not automatic updates are on, and reports the result. An update it downloads is applied on exit like any other.

The single elevation is required even though Velopack itself is installed per-user. Before replacing `current`, the old `--veloapp-obsolete` hook stops AudioEndpointBuilder and its active dependent services, including AudioSrv, so audiodg exits and releases the APO DLL. Stopping AudioSrv alone can leave the old engine loaded. After replacement, the new `--veloapp-updated` hook repeats that stop, writes the machine-wide APO registration, migrates configuration, and starts AudioEndpointBuilder before AudioSrv. If the stop fails or times out, the hook logs the service name and error, continues the install, and attempts an AudioEndpointBuilder restart rather than a bare start afterwards. The start step also runs if installation throws. Running the updater from the elevated coordinator lets both hooks inherit the same administrator token instead of prompting once per hook.

The stop captures active dependents before changing them and the same hook restores them in reverse stop order. An inactive engine is left inactive unless the updated hook owes the previous executable's obsolete hook a restart. There is no service-state handoff between those executables: updated retains the existing policy of starting AudioEndpointBuilder and AudioSrv even if they are already stopped. Other dependents captured only by the obsolete executable are not carried to the updated executable. No service's startup configuration is changed; Windows refuses to start a disabled service, and the hook reports that failure.

[Velopack's hooks documentation](https://docs.velopack.io/integrating/hooks) gives each Windows update hook 15 seconds before termination (30 seconds for install/uninstall). Obsolete allows at most 4 seconds for the complete stop, including dependent services. Updated allows 4 seconds for the stop and 4 seconds total for restoration, including a restart retry and all dependent starts. Uninstall retains its existing 3-second AudioSrv stop, 4-second AudioEndpointBuilder restart, and 3-second AudioSrv start. None of these waits receives a separate allowance per dependent service.

All service and ACL waits also share a 12-second deadline, leaving 3 seconds before Velopack's update-hook limit for other work and process startup/exit. This bounds waits, not synchronous registry or filesystem operations. For a hook invoked without elevation, the shared budget starts only in the elevated child. Time spent answering UAC does not consume this budget, but it can still consume Velopack's outer timeout.

The native download contract is documented in the [Velopack C API](https://docs.velopack.io/reference/cpp/c-api).

### The engine after an update

The APO DLL publishes its version in a read-only shared memory section (`Global\EqualizerAPO-XT.EngineBeacon.<process id>`, `runtime/ipc/EngineBeacon.h`) when the audio engine loads it, one section per process because Windows can run more than one audiodg. At startup the Editor reads the section of every running process and compares the versions with its own. If any differs, an audiodg is still running the DLL from before the update, and the Editor offers to restart Windows audio. The restart runs in a short elevated Editor process (`--eapo-restart-audio-engine`) that restarts AudioEndpointBuilder together with its dependents and then makes sure AudioSrv runs. Troubleshooting > Restart Windows audio service does the same on request.

A DLL from before this feature publishes nothing, so the Editor cannot tell that such a DLL is still loaded. The check works from the first update after the version that introduced it.

This logic lives in the owned `UpdateSession` module under `services/update/`. `VelopackBootstrap.cpp` is the SDK adapter, while `Editor/main.cpp` owns the session and decides whether an apply outcome should end the process. The channel is injected at build time with `EAPO_UPDATE_CHANNEL`.

Tests for the update session (publishing the staged version, launching the elevated coordinator, containing a background failure) live in `Tests/EditorLogicTests`.

Reference: Velopack documents the release feed (`releases.{channel}.json`) and setup assets in its distribution overview: <https://docs.velopack.io/distributing/overview>.
