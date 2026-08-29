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
