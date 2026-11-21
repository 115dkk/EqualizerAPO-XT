# Latency policy

[한국어](LatencyPolicy.ko.md)

Equalizer APO XT does not add delay to the sound unless the user asked for it.
This is part of what the project is, on the same footing as 64-bit processing:
a change that makes the default signal path later is a regression, and it is
treated like one.

This document says what that means in the code today, which parts came from
upstream Equalizer APO and which were designed here, where the user can choose
a delay, and what a contributor has to keep.

## What "latency" means here

Three different things are called latency, and the policy treats them
differently.

- **Delay the engine adds.** A fixed number of samples between a sample
  entering the engine and leaving it, caused by how the engine works:
  buffering, re-blocking, handing a block to another thread and collecting it
  later. **The policy is about this, and by default it is zero.**
- **Delay a filter is.** A linear-phase impulse response is late by half its
  length. A minimum-phase equalizer, an all-pass section and an IIR band each
  have a group delay that depends on frequency. This is a property of the
  filter the user chose, the same in every correct implementation. The engine
  neither adds to it nor hides it.
- **Delay outside the engine.** The device's buffer, the driver, the Windows
  audio engine, and another program in the chain. The engine cannot change
  these. It can avoid adding a second program and a second buffer to the chain,
  which is why the VST host, the convolver and the routing all run inside one
  process call.

## What the default path does

With the installed defaults, the APO receives a block from Windows, processes
it in place inside that same callback, and returns it. There is no queue
between input and output, so a sample leaves in the block it arrived in.

| Part | Added delay | How |
|---|---|---|
| APO block handling | 0 | Processed in the callback it arrives in (`EqualizerAPO/ApoFormat.h`). |
| Configuration change | 0 | The old and new configurations both process the same block for 10 ms and are crossfaded (`engine/FilterEngine.cpp`). |
| Convolution, any length | 0 | The partition size is the device's block size. Long responses use a two-stage convolver whose head covers the samples the tail has not finished yet (`libHybridConv-0.1.1/libHybridConv_eapo.cpp`). |
| Multi-core convolution | 0 | Workers and the audio thread finish the block together before the call returns (`runtime/concurrency/RealtimeFanOut.cpp`). |
| GraphicEQ | 0 | A minimum-phase response through the same convolver. |
| IIR filters, preamp, copy, loudness | 0 | Sample by sample. |

Three cases show the priority. Each could have been solved by buffering, and
each was solved another way.

- **A block of an unexpected size.** The convolver is built for the device's
  block size. If a block of another size arrives, the convolution is muted for
  that block and the event is counted and reported later. It is not re-blocked,
  because re-blocking means holding samples back.
- **A block larger than the announced maximum.** It is passed through
  unprocessed, not cut into pieces and queued.
- **A long impulse response.** Larger partitions are cheaper and would add a
  block of delay. The two-stage convolver keeps the small partition for the
  start of the response and spreads the rest of the work over later callbacks.

## Inherited and designed here

**From upstream Equalizer APO 1.4.2:**

- In-place processing in the APO callback.
- The 10 ms crossfade between configurations.
- Convolution partitioned at the device's block size.
- The `Delay:` command and the minimum-phase GraphicEQ.
- "Allow silent buffer modification", which lets delayed sound and tails play
  out after the input goes silent.

**Designed in this fork, with zero added delay as a requirement:**

- The two-stage convolver for long responses (#483).
- Multi-core convolution as a same-block join, and the governor that chooses
  the worker count by measured block time (#514).
- Muting instead of re-initializing or re-blocking on a block size mismatch,
  and pass-through of oversized blocks.
- The engine skipping work on silent input only when no filter has state or a
  tail to play out.
- VST delay compensation that re-reads the plugin's latency after its state is
  loaded and delays only the channels the plugin does not write, so every
  channel leaves the filter aligned.
- The ASIO wrapper's sync mode, which waits inside the buffer switch and adds
  no buffer.

## Delay the user chooses

Every one of these is either written in the configuration or set by an option
with the trade stated beside it.

| Choice | Delay | Where |
|---|---|---|
| `Delay: <t> ms` or `<n> samples` | Exactly that, on the selected channels. | Configuration |
| A VST plugin that reports latency | What the plugin reports, on every channel leaving the filter. | Configuration |
| `Hilbert:` | 512 samples on the shifted channels and on the channels named in `Align=`. | Configuration |
| `Send:` `Latency=` | D between the sending and the receiving device. Default: two processing periods of the sender. With `Compensate=` (default on) the sender's own channels are delayed by D so both devices stay together. | Configuration, Send card |
| "Resample received audio" | None beyond D: the resampler's 48-frame delay is counted inside D. The smallest D that can be asked for grows by that much. | Device Selector |
| A linear-phase impulse response | Half its length. A property of the response. | The file the user loads |
| ASIO wrapper: buffered (default) or "Remove the buffer" | Buffered adds one ASIO buffer and reports it to the application. Removing it adds none; a block that is not ready in time is then played unprocessed. | Device Selector |

The ASIO wrapper's default is the one place where the default adds delay: one
buffer, 1.3 ms at 64 frames and 48 kHz. The wrapper hands each block to a
separate engine process, and on real hardware the operating system sometimes
suspends one of the two threads for longer than a buffer. With no buffer, that
block would be heard without equalization. The buffered default was chosen for
that reason, it is reported to the application so recording stays aligned, and
the option to remove it is one checkbox. The APO path, which is what most users
run, has no such hand-off and no such buffer.

## Knowing the number

- `FilterEngine::getRealLatency()` returns the fixed delay the loaded
  configuration adds (the largest among the output channels;
  `getRealLatencyByChannel()` gives each one). Benchmark prints it
  (`Real latency: <n> samples`). It counts the rows of the table above that are
  a fixed sample count and nothing else.
- The APO's `GetLatency` does not include that figure. It reports zero, plus a
  vendor APO's own latency when one is attached, as upstream does. Changing
  what Windows is told can change how Windows schedules the stream, so it is
  left alone.
- The ASIO wrapper adds its one buffer to the latency it reports to the
  application.
- The Editor's analysis panel shows the delay it removed before measuring, and
  "Include base delay" puts it back into the phase and group-delay curves.

## Not done yet

- Crossfading between two configurations whose fixed delays differ mixes them
  without aligning them. For 10 ms the two are heard at different times.
- `Send:` `Latency=Auto`, which would follow the receiving device's period, is
  not implemented.
- The real-latency figure has not yet been compared against a loopback
  measurement of the installed APO.

## What to keep

1. **The default path adds no delay.** A change that puts a queue, a second
   block size, or a hand-off across a block boundary into the default path is
   not accepted, whatever it saves in CPU.
2. **Spread work inside the block, not across blocks.** More threads, cheaper
   arithmetic and work deferred behind a head that already covers it are all
   acceptable. Returning a block later is not.
3. **When the choice is between delay and an honest failure, fail.** Mute,
   pass through, or play the block unprocessed, count it, and report it off the
   audio thread. Do not buffer silently to hide the problem.
4. **Delay is opt-in and exact.** A feature that needs delay gets an option or
   a configuration parameter, defaults to none where that is possible, and
   states its cost next to the control. An exception needs a measured reason
   written down, as the ASIO buffer has.
5. **Channels stay aligned.** A filter that delays some channels provides the
   means to delay the others by the same amount (VST compensation, `Align=`,
   `Compensate=`).
6. **Report it.** A filter that adds a fixed delay overrides
   `IFilter::reportLatency`, so the real-latency figure stays true.
7. **Test it.** A new or changed signal path gets a test that compares sample
   positions, not only levels. The existing ones are the model: the two-stage
   convolver against direct convolution, VST compensation against a
   latency-512 plugin, Send's exact D, and sync mode adding nothing.
8. **Do not claim what was not measured.** A number in the documentation comes
   from a test, a measurement that is described, or the code.
