# Velopack Update Checks

EqualizerAPO-XT publishes installers through the Velopack release job in GitHub Actions. Velopack creates channel-specific assets such as `releases.x64-avx2.json`, `EqualizerAPO-XT-x64-avx2-...-full.nupkg`, and `EqualizerAPO-XT-x64-avx2-Setup.exe`.

`UpdateChecker.exe` now checks the latest GitHub Release for `115dkk/EqualizerAPO-XT` instead of the upstream SourceForge version endpoint. The existing scheduled task still runs it with `-a` at logon, keeps the 24 hour automatic-check throttle, and respects the locally skipped version.

The update flow is:

1. Detect the installed build channel.
2. Request the latest GitHub Release.
3. Prefer the matching Velopack feed asset, `releases.<channel>.json`.
4. Read the newest `Full` package for the current channel and compare it with `version.h`.
5. Open the matching channel setup asset when the user accepts the update.

The channel is injected by CI with `EAPO_UPDATE_CHANNEL` during qmake builds. Current CI channels are:

- `x64-avx2`
- `x64-avx512`
- `x64-avx10-1`
- `arm64`

Local builds without an injected channel default to `x64-avx2` on x64 and `arm64` on ARM64.

The actual APO installation and device registration are still handled by the NSIS installer inside the Velopack package. Until the project embeds a native Velopack client, UpdateChecker uses Velopack feeds for discovery and sends users to the correct channel setup executable.

Tests for feed selection, channel matching, setup URL selection, and version comparison live in `Tests/EditorLogicTests`.

Reference: Velopack documents the release feed (`releases.{channel}.json`) and setup assets in its distribution overview: <https://docs.velopack.io/distributing/overview>.
