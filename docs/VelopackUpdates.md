# Velopack Update Checks

EqualizerAPO-XT publishes installers through the Velopack release job in GitHub Actions. Velopack creates channel-specific assets such as `releases.x64-avx2.json`, `EqualizerAPO-XT-x64-avx2-<version>-x64-avx2-full.nupkg`, and `EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe` (the channel appears twice because Velopack appends it to a pack id that already carries it; `.github/scripts/ReleaseAssets.psm1` owns the spelling).

The Editor's in-app Velopack update (below) is the only update path. It reads the channel's feed, `releases.<channel>.json`, from the latest GitHub Release of `115dkk/EqualizerAPO-XT`. The standalone `UpdateChecker.exe`, a notify-only tool that nothing started automatically after the NSIS installer left (audit #250 F073), was removed in audit #348.

The channel is injected by CI with `EAPO_UPDATE_CHANNEL` during qmake builds. Current CI channels are:

- `x64-sse2`
- `x64-avx`
- `x64-avx2`
- `x64-avx512`
- `x64-avx10-1`
- `arm64-neon`

Local builds without an injected channel default to `x64-avx2` on x64 and `arm64-neon` on ARM64.

APO installation and device registration run through the Velopack hooks the Editor handles (`--veloapp-install`, `--veloapp-updated`, etc.), which call `ApoRegistration`. The NSIS installer has been removed.

## Editor in-app auto-update

The Editor embeds the native Velopack client (`velopack_libc`) and updates itself without sending the user anywhere:

1. On a normal launch the Editor calls `Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run()`. Auto-apply on startup is off because updates are applied on exit instead.
2. About 60 seconds after start (only for Velopack installs), a background worker checks the GitHub release feed for the build's channel with `UpdateManager::CheckForUpdates()` and, if a newer build exists, downloads it with `DownloadUpdates()` into the Velopack staging area. The download runs off the GUI thread and never blocks shutdown.
3. When the Editor exits with an update staged, it asks for elevation once and launches a short-lived elevated Editor coordinator. The coordinator reopens the staged package with `UpdatePendingRestart()`, calls `WaitExitThenApplyUpdates(info, silent: true, restart: false)`, and exits. The updater inherits that token, waits for the coordinator to close, swaps the files silently, and does not relaunch. The new version comes up on the next launch.

The single elevation is required even though Velopack itself is installed per-user. Before replacing `current`, the old `--veloapp-obsolete` hook must stop the Windows audio service so the loaded APO DLL no longer locks the directory. After replacement, the new `--veloapp-updated` hook writes the machine-wide APO registration and restarts the service. Running the updater from the elevated coordinator lets both hooks inherit the same administrator token instead of prompting once per hook.

This logic lives in the owned `UpdateSession` module under `services/update/`. `VelopackBootstrap.cpp` is the SDK adapter, while `Editor/main.cpp` owns the session and decides whether an apply outcome should end the process. The channel is injected at build time with `EAPO_UPDATE_CHANNEL`.

Tests for the update session (publishing the staged version, launching the elevated coordinator, containing a background failure) live in `Tests/EditorLogicTests`.

Reference: Velopack documents the release feed (`releases.{channel}.json`) and setup assets in its distribution overview: <https://docs.velopack.io/distributing/overview>.
