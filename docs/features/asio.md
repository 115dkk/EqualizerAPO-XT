# ASIO

EqualizerAPO-XT applies the same `config.txt` to ASIO streams. An ASIO
application (a DAW, foobar2000's ASIO output, a game with an ASIO backend)
picks the driver entry `<your driver> (EQ APO XT)` instead of `<your driver>`,
and everything the config says for that device runs on the way to the
hardware and, for inputs, on the way back from it. Nothing else changes: the
same Device Selector installs it, the same Editor edits it, the same file
configures it. The design and its measurements are in
[docs/architecture/asio-host-study.md](../architecture/asio-host-study.md).

## Turning it on

1. Open the Device Selector. Every ASIO driver on the machine appears in the
   playback list and in the capture list, in the same groups as the Windows
   endpoints, as `ASIO <driver name>`; the leading word is the only marker.
2. Tick the playback entry to process what the application sends to the
   interface, the capture entry to process what the interface records, or
   both. Confirm.
3. In the application, choose `<your driver> (EQ APO XT)` as the ASIO driver.
   The original entry stays available and bypasses EqualizerAPO-XT.

The first time the application opens the device, `EqualizerAPOHost.exe`
starts, loads `config.txt`, and only then does the device open: the first
buffer that reaches the hardware is already processed. The host leaves a
minute after the last stream ends.

## What the config sees

An ASIO stream is a device like any other for `Device:` lines. Its string is
`ASIO <driver name> {driver CLSID}`, so `Device: ASIO` selects every ASIO
stream and `Device: Topping` selects one interface. Without a `Device:` line a
filter applies to ASIO streams as well as to endpoints. Channel names follow
the channel count the way they do for endpoints (2 -> `L R`, 6 -> 5.1,
8 -> 7.1, otherwise `1`, `2`, ...); the engine sees every physical channel of
the interface, so `Channel:` names do not move when the application opens
only some of them. `Stage: capture` blocks apply to the input direction,
everything else to the output direction.

The Editor lists an installed ASIO device in its device menu; its toolbar
badge shows the rate and channel count of the last stream once one has run
(the host records them then), and says so when none has.

## Latency and the two modes

By default the wrapper hands each buffer to the host and plays the previous
one: one buffer of extra latency (1.3 ms at 64 frames and 48 kHz), reported
to the application through the driver's latency query, and no dependence on
how quickly the host answers. Measured on a Topping USB Audio Device at 64
frames over ten minutes (450,005 buffers), this mode processed every buffer;
all but three round trips took under 100 microseconds and the worst, 2.5 ms,
was absorbed by the one-buffer pipeline.

The synchronous mode waits for the host inside the buffer callback instead
and adds no latency, but a buffer whose answer misses the deadline passes
through unprocessed. On the same interface a few buffers per minute missed,
from the operating system preempting one of the two threads, so it is not the
default. It can be selected per driver in the Device Selector: select the
driver's entry, open the troubleshooting options and tick **Remove the
buffer**; it applies to both directions. Once ticked, **Wait time** unfolds
beside it: how long a buffer waits for the host before it comes out without
the EQ, up to a quarter of the buffer (the default), half, or three
quarters. A longer wait misses fewer buffers and leaves the application less
of the buffer for its own work. The two remaining options sit side by side
under that row.

## Options in the Device Selector

Besides the synchronous mode and its wait time, an ASIO entry's
troubleshooting panel has two options, both off by default:

- **Start the engine host automatically at boot** writes one `Run` value for
  the machine so the host is up before any application opens the driver.
  Otherwise the first application starts it and it leaves a minute after the
  last one closes the driver. The value stays while any driver asks for it;
  after the option is turned off, a host already running stays until
  sign-out.
- **32-bit host support** also registers the driver entry where 32-bit
  applications look for it (`WOW6432Node`). Unavailable on the ARM64 build,
  which ships no 32-bit wrapper.

The record behind an entry lives under
`HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO\ASIO\<wrapper CLSID>`: `Mode`,
`DeadlinePercent`, `DeadlineUs` (an explicit microsecond budget, normally 0),
`AutoStart`, `Register32`.

## When something is off

- The application refuses to open the device with a driver error: the host
  could not start or did not load the configuration in time. The reason is
  in `EqualizerAPOAsio.log` and `EqualizerAPOHost.log` under
  `%LOCALAPPDATA%\EqualizerAPO\logs`.
- The interface's sample rate changes while a stream runs: the wrapper asks
  the application to reopen the device, and buffers pass through unprocessed
  until it does.
- The host crashes mid-stream: buffers pass through unprocessed for the rest
  of that session; reopening the device starts a fresh host. The DAW is not
  affected beyond that.
- 32-bit applications see the entry only with **32-bit host support** ticked
  for the driver; x64 builds ship the 32-bit wrapper, which talks to the
  same 64-bit host. The ARM64 build has no 32-bit
  wrapper, and its wrapper is ARM64-native: an x64 application running under
  emulation on an ARM64 machine cannot load it (the 64-bit registry view is
  shared, so one entry cannot serve both architectures without an ARM64X
  binary, which this build does not produce).
- DSD streams are not supported; the device refuses to open in a DSD mode.

## License

The wrapper is built against the Steinberg ASIO SDK 2.3.4 under the SDK's
GPL version 3 option, so `EqualizerAPOAsio.dll` and the installers that ship
it are distributed under GPL version 3; the text is installed as
`License-gpl-3.0.txt` next to the program. ASIO is a trademark and software
of Steinberg Media Technologies GmbH.

## What is not covered

The wrapper has been exercised with the fake driver on CI and with a Topping
USB Audio Device on the maintainer's machine. It has not been run under every
DAW; hosts that process outside the buffer callback without calling
`ASIOOutputReady` get their first buffer committed early once, before the
wrapper learns their habit.
