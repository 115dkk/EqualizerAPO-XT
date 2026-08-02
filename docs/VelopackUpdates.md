# Velopack Update Checks

EqualizerAPO-XT publishes installers through the Velopack release job in GitHub Actions. Velopack creates channel-specific assets such as `releases.x64-avx2.json`, `EqualizerAPO-XT-x64-avx2-...-full.nupkg`, and `EqualizerAPO-XT-x64-avx2-Setup.exe`.

`UpdateChecker.exe` checks the latest GitHub Release for `115dkk/EqualizerAPO-XT` instead of the upstream SourceForge version endpoint. Its `-a` automatic mode keeps the 24 hour check throttle and respects the locally skipped version. Note (audit #250 F073): nothing registers a logon scheduled task for it anymore - that registration left with the NSIS installer. Automatic checking today is the Editor's in-app Velopack update; UpdateChecker is a manual discovery/notification tool until a logon check is deliberately reintroduced.

The update flow is:

1. Detect the installed build channel.
2. Request the latest GitHub Release.
3. Prefer the matching Velopack feed asset, `releases.<channel>.json`.
4. Read the newest `Full` package for the current channel and compare it with `version.h`.
5. Open the matching channel setup asset when the user accepts the update.

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

This logic lives in `helpers/VelopackBootstrap.cpp` (`startBackgroundDownload`, `hasPendingUpdate`, `applyPendingUpdateAndExit`) and is wired into `Editor/main.cpp`. The channel is injected at build time with `EAPO_UPDATE_CHANNEL`, the same macro UpdateChecker uses.

`UpdateChecker.exe` stays as a separate discovery/notification tool: run manually (or with `-a`), it checks the GitHub release feed and tells the user when a newer version is available; the Editor performs the actual download and apply.

Tests for feed selection, channel matching, setup URL selection, and version comparison live in `Tests/EditorLogicTests`.

Reference: Velopack documents the release feed (`releases.{channel}.json`) and setup assets in its distribution overview: <https://docs.velopack.io/distributing/overview>.
