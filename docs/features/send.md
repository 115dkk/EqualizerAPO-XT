# Send: handing channels to another endpoint

`Send:` lets the configuration of one playback endpoint hand some of its
channels to the equalizer of another playback endpoint. It exists for
interfaces that Windows shows as several stereo endpoints although they are
one device. The case that started it is a Topping E4x4 Pre, which appears as
Playback 1/2 and Playback 3/4, so a 2.2 setup with two subwoofers on outputs
3 and 4 cannot be built from one configuration. With Send it can:

```
Device: Playback 1/2
NewChannel: SUB1 SUB2
Copy: SUB1=0.5*L+0.5*R SUB2=0.5*L+0.5*R
Include: secs_room_correction.txt
Send: {GUID-of-Playback-3/4} L=SUB1 R=SUB2
```

The receiving endpoint has no command. A `Device: Playback 3/4` block may
hold filters, and they apply to what it receives. No virtual cable and no
kernel driver are involved.

The user-facing grammar is in the configuration reference
(`Wiki/github-wiki/Configuration-reference.md`, section Send). This document
describes how it works and what was and was not measured.

## Where it runs

Both endpoints' APO instances run in the audio engine process (`audiodg.exe`,
session 0, LOCAL SERVICE). Send uses a shared-memory ring and an event named
in the `Local\` namespace of that session, so it works only between
endpoints of the audio service. It is inactive in every other host of the
engine: the ASIO engine host runs in the user session and cannot see
session 0's `Local\` names, and the Editor's analysis engine must not create
kernel objects. Those hosts recognize the line and do nothing with it
(`EngineSetup::host`).

Sending and receiving happen only in a post-mix APO instance. A pre-mix
instance exists once per stream, so two programs playing on one endpoint
would make two senders, or receive the same audio twice. On an endpoint
without a post-mix APO the Send line is refused with a log line.

## The ring (`runtime/ipc/SendRing`)

One mapping per receiving endpoint, named
`Local\EAPO.Send.{receiving endpoint GUID}`, and a manual-reset event with the
same name plus `.ready`. Whichever side comes first creates them; the size is
fixed, so it does not matter which. The region is a 2048-byte header and 16
float32 planes of 32768 frames (2 MiB). The specification planned a 512-byte
header; the 16 target channel names alone take 1024 bytes.

The header carries the sender's identity, its sample rate, the channel count,
the latency D in frames, the target channel names, the mix mode, a
generation that changes whenever any of these change, and two per-block
cursors: `writePos` (frames ever written) and `writeQpc` (when the last block
was written). The receiver writes its diagnostics (read position, underruns,
drift steps) on a separate cache line the sender never reads.

The pure part (header, cursors, attach arithmetic, drift correction) runs on
any block of memory and is pinned by
`Tests/EngineOrchestrationTests/SendRingTests.cpp` with explicit clock
values. The Win32 part only opens the mapping and the event.

## Sending

`SendFilter` sits where its line is in the chain, so every filter above it is
in what it sends. Each block it computes the assignments' sums and writes them
to the ring. The ring and its cursor belong to the factory, which lives as
long as the engine, so a configuration reload does not restart the ring.
While a reload crossfades, the old and the new configuration both process the
block; the writer takes only the first write of a block (`blockCounter()`
token), which comes from the configuration being faded out. What is sent
therefore switches to the new configuration when the crossfade ends, without
a crossfade of its own. A failure to create the ring refuses that Send line
only; on the receiving side it leaves the endpoint playing without receiving,
and in both cases the reason is logged.

**Latency D.** Unless the line says otherwise, D is twice the sending
engine's maximum block length. The receiver reads at the position D frames
behind the sender's write position at the moment it attaches (corrected for
the wall-clock time since the sender's last block), so the two endpoints'
outputs are D apart in every session, give or take scheduling jitter. A
receiving endpoint with a longer period than the sender can need more than
the default; the receiver counts underruns and the log suggests a larger
`Latency=`.

**Compensation.** With `Compensate=true` (the default) the sending engine
delays its own device channels by D, so both endpoints play in time. This is
one delay filter appended at the end of the chain, with the largest D of the
compensating lines; the ring gets the undelayed signal. This matters for
room-correction filters that align mains and subwoofers together: without
it the subwoofers would play D late. The delay is not reported to Windows
through `GetLatency`, like `Delay:`.

**One sender per target.** A sender that finds another live sender in the
ring (a different identity whose last block is under a second old) refuses
its line, and so does a second line in one configuration naming the same
target. A sender that has been idle for a second is not live, so a line in
another endpoint's configuration can take the ring over; the idle sender then
stops writing to it for good, and only the newer one is heard.

## Receiving

The factory of a post-mix APO's engine installs an input tap
(`engine/IInputTap.h`). The engine applies it to every configuration's
channels after the virtual channels are zeroed and before any filter runs,
which is why filters in the receiving endpoint's `Device:` block apply to the
received audio. During a reload crossfade the tap is applied to both
configurations with the same block token; it reads the ring once and adds the
same samples twice.

`Mode=Mix` adds the received channels to what the endpoint already plays;
`Mode=Replace` overwrites the targeted channels. Targets are resolved
against the receiving endpoint's own channel names (aliases included); a name
it does not have is ignored and logged once.

**Attaching.** The receiver opens the ring and attaches when its engine
initializes, when its configuration reloads, and when the ready event is
signalled: the sender signals it whenever it publishes a new header, and the
configuration watcher treats the signal like a change to `config.txt`.

**Leaving.** When the sender writes `Closing` or publishes a different
identity or generation, the receiver adds nothing until it attaches again;
both come with a signal on the ready event, so that happens on its own.

**Idle senders.** Windows calls the sending APO only while something plays on
the sending endpoint. While the sender has not written for a second (or has
not written at all yet), the receiver stays attached and adds nothing; this is
not counted as an underrun, and `Mode=Replace` leaves the channels as they
are. On the first block the sender writes again, the receiver sets its read
position by the same rule as attaching (D behind the sender, corrected for
the time since that block). It does the same when it finds itself a whole
block ahead of the sender, which happens when the sender skipped blocks
without going idle; those blocks count as underruns.

**Drift.** Sibling endpoints of one device share a clock and do not drift.
For endpoints on different clocks the receiver measures how far behind the
writer it reads (the average over the first 50 blocks after attaching) and
keeps a slow average of it afterwards; when the average moves more than half
a block away, the read position steps by one frame (a repeated or dropped
sample, inaudible in the subwoofer band). Steps are counted.

## The keepalive stream

Windows runs an endpoint's APO only while a stream plays there. The Device
Selector's **Send** option on the receiving endpoint therefore:

1. writes `ReceiveFromEndpoints = true` under
   `HKLM\SOFTWARE\EqualizerAPO\Child APOs\{guid}`,
2. turns on *Allow silent buffer modification* for it, because the
   keepalive stream's input is silence and without that option the APO
   discards its output on silent input,
3. makes `EqualizerAPOHost.exe --resident` start at logon (`Run` value),
   alongside the ASIO wrapper's reason for it.

The resident host keeps a shared-mode, event-driven render stream open on
every such endpoint, at the engine's default period, and submits buffers of
zeros **without** `AUDCLNT_BUFFERFLAGS_SILENT`.

Before anyone logs on there is no resident host and therefore no
receiving.

## Tests

- `Tests/EngineOrchestrationTests/SendRingTests.cpp`: the ring alone, with
  explicit clock values (attach arithmetic, idle and resuming senders, drift
  steps, underruns, a second sender taking the ring over).
- `Tests/EngineOrchestrationTests/SendTests.cpp`: two engines in one process
  over the real Win32 mapping and event (routing, compensation, both modes,
  crossfades, refusals, a receiver that starts before the sender writes).
- `Tests/ApoHostProbe --send-to`: `EqualizerAPO.dll` hosted twice in one
  process for two playback endpoints, with the real registry and the real
  configuration file, the way audiodg hosts them but without it. On the
  development machine (CABLE-A Input sending to CABLE-B Input, a block
  `Device: {CABLE-A}` / `Send: {CABLE-B} L=L R=R Compensate=false` appended
  to the configuration for the run) the receiving instance played the
  sender's tone at 0.000 dB. The receiver attached before the sender's first
  block, which is the order the audio service produces when the receiving
  endpoint runs first.
- The capture gate in CI runs that probe round when the runner shows a second
  active playback endpoint besides VB-CABLE's `CABLE Input`, and lists the
  runner's active playback endpoints in its summary either way. Without a
  second one the round is skipped and the summary says so. The first run
  (PR #409, windows-2022 runner, VB-CABLE Driver Pack 43) showed one active
  playback endpoint, `CABLE Input`, so the round is skipped on hosted
  runners as they are today.

## Not measured

The following were not measured on the development machine, which has one
active endpoint with the APO installed (`CABLE Input`). Each has a fallback
built in or a place in this document to record the result.

- **Whether audiodg calls a post-mix APO every period for zero-filled,
  unflagged buffers.** If it does not, set
  `HKLM\SOFTWARE\EqualizerAPO` `SendKeepaliveFill` (REG_SZ) to `dither`;
  the host then fills about -100 dBFS TPDF dither. The value is read when a
  stream opens.
- **The time difference between the two physical outputs across sessions**
  (it should equal D every time).
- **Topping E4x4 Pre**: which endpoint wins when the two run at different
  sample rates, and whether the hardware offset between its endpoints is the
  same in every session. If it is not, `SubwooferRouting:` path delays have
  to be re-measured per session on that device.

Only one `audiodg.exe` process ran on the development machine, so the two
APO instances share one process as assumed; a machine with the APOs in two
processes of one session would still share the `Local\` namespace.

## Decisions recorded

- The Device Selector option is named **Send** (Korean 전송).
- `Latency` takes milliseconds by default, and whole samples with the
  `samples` suffix attached (`Latency=960samples`); a detached unit would be
  read as a broken assignment.
- Mix or replace is chosen per Send line (`Mode=`), shown as a dropdown on the
  Send card; mixing is the default.
- Send sits with Device and Channel in the picker's Control group.
- **Latency negotiation** (D following the receiver's period) is a follow-up.
  It belongs on the Send line as `Latency=Auto`, because the sender owns D and
  each route can differ. The default stays the fixed rule until the
  negotiation is measured. It is not implemented here because the receiver's
  period is unknown until it attaches, and a D that changes at run time means
  reloading the sender's configuration to rebuild the compensation delay.
