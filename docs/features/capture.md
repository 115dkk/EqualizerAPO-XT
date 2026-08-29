# Recording (capture) devices

Equalizer APO installs on a recording endpoint the same way it installs on a
playback endpoint: the Device Selector writes the APO's CLSID into the
endpoint's effect chain (`FxProperties`) and the audio engine loads the DLL
for every stream a recording app opens on that endpoint. The whole
`config.txt` applies, once, unless a `Stage:` or `Device:` line says
otherwise; `stage` reads `capture` for an `If:` line. The engine side of that
contract is pinned by `Tests/EngineOrchestrationTests/CaptureEngineTests.cpp`.

What differs from playback:

- A recording endpoint carries one APO, in the pre-mix (stream) slot. The
  post-mix options in the Device Selector are disabled for it.
- Most microphones and virtual cables publish no effect chain of their own,
  so installing creates the `FxProperties` key the driver never made. That
  is the case the Device Selector used to call "experimental".
- A recording app chooses a signal processing mode through its stream
  category. Plain recorders run in the default mode; voice-chat apps tag
  their streams *Communications*; an app that asks for *raw* capture gets no
  stream effects at all, by Windows' design, and no EQ.

## Measuring it

Two probes, built with the solution, answer "does the EQ reach a recording
app" without a person listening:

- `Tests/ApoHostProbe` hosts `EqualizerAPO.dll` for one endpoint the way the
  audio engine does, without the audio engine: it loads the DLL through its
  own class factory, tells it the endpoint's GUID, negotiates a float
  connection and pushes a sine through. The DLL then reads the endpoint's
  record, learns it is a recording endpoint, loads the registry's config and
  filters. Runs unelevated on any machine.

  ```
  ApoHostProbe --dll <install>\EqualizerAPO.dll --endpoint {capture-endpoint-guid}
  ```

- `Tests/CaptureProbe` plays a sine into a playback endpoint and records
  from a capture endpoint at the same time, through WASAPI shared mode (the
  path every recording app uses), and reports the tone's level. Over a
  virtual cable with the APO on the cable's recording side and
  `Preamp: -20 dB` in the config, the tone must arrive 20 dB down.
  `--category communications` and `--raw` select the stream's processing
  mode.

  ```
  CaptureProbe --list
  CaptureProbe --render "CABLE Input" --capture "CABLE Output" --json
  ```

The Device Selector has a headless form of its OK button for one endpoint,
`DeviceSelector --install-endpoint {guid}` (and `--uninstall-endpoint`),
which runs the same install and the same device test the dialog runs and
exits 0 when the APO reported itself alive from inside the audio engine.
`--install-mode lfx-gfx|sfx-mfx|sfx-efx` pins a slot pair. It needs
elevation, like the dialog, and writes what it did to `DeviceSelector.log`.

## The CI gate

`.github/scripts/Invoke-CaptureGate.ps1` (job `capture-gate` in
`build.yml`) puts the three together on a hosted runner: it installs
VB-CABLE (a signed virtual cable driver, pinned by SHA-256), stages the
built product through its own install hook, and measures the cable's
recording side before and after `--install-endpoint`, in the default,
communications and raw modes, once per install mode. Every measurement and
the audio engine's own APO log land in the `capture-gate-snapshots`
artifact.

## What the gate measured

Third run, Windows Server 2022, VB-CABLE Driver Pack 4.3, 44.1 kHz stereo,
a 1 kHz tone at -9 dBFS, `Preamp: -20 dB`:

| round | slot written | default mode | communications | after uninstall |
|---|---|---|---|---|
| product's own choice | LFX | -20.01 dB | -20.01 dB | -0.01 dB |
| `--install-mode lfx-gfx` | LFX | -20.01 dB | -20.01 dB | -0.01 dB |
| `--install-mode sfx-efx` | SFX | -0.01 dB | -0.01 dB | -0.01 dB |
| `--install-mode sfx-mfx` | SFX | -0.01 dB | -0.01 dB | -0.01 dB |

In the SFX rounds the audio engine's own log shows no `Initialize` for the
endpoint and the device test never hears from the APO: an SFX registration
on a driver that declares no signal processing modes is not loaded at all.
That is why a device without a driver effect chain keeps the legacy
LFX/GFX slot pair (`DeviceAPOInfo::load`), and why the candidate that
moved such devices to the stream slot was dropped. The cable's driver
declares no raw mode either (`AUDCLNT_E_RAW_MODE_UNSUPPORTED`), so the
raw measurement is only informational.

Two things the gate cannot tell: how a mode-aware driver (Realtek, Intel
SST) behaves - there, the stream slot is the documented one and the
processing-mode list decides which streams reach the APO, which is what
the mode-list change is for - and what the field report's own machine
does. On such a machine, `ApoHostProbe` against the microphone's GUID
tells whether the DLL processes for it, and `CaptureProbe --capture
"<microphone>"` with a tone played into the room tells whether the audio
engine runs it.

## The low-latency round

The same gate has a round for the playback side and the engine's small
buffers (`docs/architecture/wasapi-exclusive-study.md`, section 3). A
low-latency application (a game, a DAW, a voice-chat app) asks for a small
period through `IAudioClient3`, and every stream on that endpoint and mode
moves to it; the APO stays in the path and simply gets shorter blocks. The
one filter that notices is the convolution, which processes at the block
length it was locked with and goes silent on any other. So the round puts a
unit impulse (`gate-impulse.wav`, unity) behind the preamp, installs the APO
on `CABLE Input`, and measures three ways with `CaptureProbe --period`:

- `ll-default`: the convolution config at the default period, the reference.
- `ll-min`: a fresh tone stream at the smallest period the driver declares.
- `ll-min-switch`: a silent default-period stream is held open first
  (`--hold-default`), then the small-period stream starts, so the engine has
  to switch a running graph with a post-mix APO already locked at the
  default count.

Each is expected at the preamp. Silence in either small-period measurement
means the engine hands the post-mix APO blocks shorter than the count it
locked with, and the convolution needs to be made independent of the block
length. The probe's JSON carries the engine's default and minimum periods
and the period it actually ran at; the summary lists every `LockForProcess`
frame count the DLL traced during the run. On a driver that declares no
period below the default the two small-period measurements are recorded as
skipped, not failed.

That is what the hosted runner's cable does: VB-CABLE Driver Pack 4.3
declares no period below the default, so the gate records the convolution
config at -20.01 dB at the default period and skips the two small-period
measurements, with the `LockForProcess` frame counts of the run (485, 480,
528) in the summary. The judgement itself was made on the maintainer's
VB-CABLE 3.3.1.7, which declares 96 frames: with the same preamp and
convolution behind the endpoint's post-mix APO, the far side read -20.00 dB
at the default period, at 96 frames fresh, at 96 and at 128 frames after a
running default-period stream was switched, and at the default period
again, with no silent packets in any of them. The engine locks the APO
again for the new period; a convolution follows it.
