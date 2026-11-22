# Latency benchmark

[한국어](LatencyBenchmark.ko.md)

This page holds the measurements behind the [latency policy](LatencyPolicy.md).
It compares Equalizer APO XT with CamillaDSP and Omniphony on one task:
upmixing stereo music and rendering it to headphones through a multichannel
BRIR.

The short version, on the maintainer's PC:

- XT added **0 samples** in all three scenes, measured through the real Windows
  audio engine.
- CamillaDSP, fed a signal that was already upmixed, added 169 to 196 ms at its
  lowest setting that did not drop out.
- CamillaDSP behind the zAuro standalone application, which is how the chain is
  actually used, added 300 to 360 ms, and the standalone's output had gaps in
  about one run in three.

## What was measured

| Scene | Upmix | Convolutions |
|---|---|---:|
| 7.1 | zAuro, 7.1 output | 14 |
| 7.1.4 | zAuro, 7.1.4 output (the maintainer's daily setting) | 22 |
| 9.1.6 | zAuro, 7.1.4 output, plus four channels made from their neighbours | 30 |

Each speaker is convolved twice, once per ear, with a 2-second BRIR at 48 kHz.
LFE is folded into the centre. zAuro's widest output is 7.1.4, so the 9.1.6
scene adds wide left/right and top-middle left/right as the mean of the two
neighbouring channels, with BRIR tracks made the same way. It is a load test,
not a listening setup.

The input is 20 to 30 seconds of a music track from a game installed on the
PC, played as it is. The track, the BRIR and zAuro are not in this repository
and were not copied out of the measurement.

### Machine

Intel Core i5-12600KF (10 cores, 16 threads), Windows 11, Equalizer APO XT
2.77.0, CamillaDSP 4.1.3, zAuro 0.4.1, VB-CABLE with 16-channel endpoints at
48 kHz.

### Method

One player opens a single duplex stream on the virtual cable and never stops
it. It plays the music to the endpoint under test and, in the same stream, to
two spare channels that nothing processes. The recording of the processed
channels and the recording of the spare channels are each aligned to what was
played by cross-correlation. The difference between the two lags is the delay
the program added. The player's own buffers and the cable are in both and
cancel.

The processed recording is compared with XT's offline output for the same
input (`Benchmark.exe`). A residual near -70 dB means the program produced
the same sound, and a 10 ms block that stands out from the rest marks a gap.
For CamillaDSP, a setting counts as clean only if its log also reports no
underrun during the music.

zAuro reports 4351 samples (90.6 ms) of its own latency. Every chain here
contains zAuro, so the reference contains it too and it is not counted as
delay added by any program.

CamillaDSP lets the user choose the delay through `chunksize`. Each scene was
swept from 1024 down to 64, single-threaded and multithreaded, and the figure
quoted for CamillaDSP is the lowest one where no dropout was reported.

## XT

XT ran as the APO on the cable's playback endpoint, inside the Windows audio
engine. Then its configuration for that endpoint was removed in the same
stream, as the bypass.

| Scene | Delay added | Residual against offline output | Gaps | Audio engine CPU, XT on | XT off |
|---|---:|---:|---:|---:|---:|
| 7.1 | 0 samples | -71.0 dB | 0 | 38% | 2% |
| 7.1.4 | 0 samples | -73.5 dB | 0 | 45% | 2% |
| 9.1.6 | 0 samples | -76.6 dB | 0 | 65% | 3% |

CPU is the processor time of `audiodg.exe` over the music, as a share of one
core. It includes zAuro. With the convolution taken out and only zAuro left,
the same process used 12 to 13%, so XT's convolution cost about 25%, 33% and
53% of one core in the three scenes.

The 7.1.4 scene was run four times in all. The player's own lag differed from
run to run (5407 to 5882 samples) and the processed and spare channels had the
same lag to the sample every time.

## CamillaDSP after an upmix that is already done

Here CamillaDSP received the upmixed channels as a ready-made signal, so the
upmixer costs it nothing. The delay is CamillaDSP itself plus the second trip
through the cable that any separate program needs.

CPU is CamillaDSP's processor time as a share of one core.

**7.1 (14 convolutions)**

| Chunk | Threads | Delay added | Dropouts | CPU |
|---:|---|---:|---|---:|
| 1024 | single | 214 ms | none | 13% |
| 512 | single | 206 ms | none | 22% |
| 256 | single | 193 ms | none | 45% |
| 256 | multi | 184 ms | none | 291% |
| 128 | single | - | yes | 96% |
| 128 | multi | 182 ms | none | 523% |
| 64 | multi | **169 ms** | none | 1013% |

**7.1.4 (22 convolutions)**

| Chunk | Threads | Delay added | Dropouts | CPU |
|---:|---|---:|---|---:|
| 1024 | single | 213 ms | none | 18% |
| 512 | single | 204 ms | none | 34% |
| 256 | single | 194 ms | none | 65% |
| 256 | multi | **182 ms** | none | 427% |
| 128 | single | - | yes (1118 underruns in 20 s) | 102% |
| 128 | multi | 191 ms | none | 827% |
| 64 | single | - | yes | 101% |
| 64 | multi | - | yes | 1140% |

**9.1.6 (30 convolutions)**

| Chunk | Threads | Delay added | Dropouts | CPU |
|---:|---|---:|---|---:|
| 1024 | single | 213 ms | none | 26% |
| 512 | single | 208 ms | none | 49% |
| 256 | single | - | yes | 95% |
| 256 | multi | **196 ms** | none | 634% |
| 128 | single | - | yes | 100% |
| 128 | multi | - | yes | 1228% |
| 64 | multi | - | yes | 1201% |

Every clean row matched XT's offline output at -69 to -72 dB. The two programs
produce the same sound.

Two things stand out. Shrinking the chunk hardly lowers the delay: from 1024
to 256 it falls by about 30 ms and roughly 170 ms stays. And each halving of
the chunk about doubles the CPU, until one core is not enough and then until
twelve are not enough.

## CamillaDSP behind the zAuro standalone

A CamillaDSP user has no upmixed signal to start from. The music goes to a
virtual cable, an upmixer application reads it and writes twelve channels back,
and CamillaDSP reads those. This is the chain XT replaces with one process
call, so this is the comparison that counts.

The zAuro standalone ran with the cable as its input and output device in
Windows shared mode. In that mode it offered one buffer size, 480 samples.

**The standalone alone** added 128 to 145 ms over six runs (6129 to 6950
samples) and used 27 to 29% of one core. In two of the six runs its output had
gaps: the residual against XT's output of the same plugin was -17 dB with 11
to 17 marked blocks, against -75 dB and none in the other four.

**The whole chain**, three runs per setting:

| Scene | CamillaDSP setting | Delay added, each run | Runs with gaps | CPU (CamillaDSP + zAuro) |
|---|---|---|---:|---|
| 7.1.4 | 256, multi | 317, 330, 328 ms | 1 of 3 | 440% + 46% |
| 7.1.4 | 256, single | 326, 343, 323 ms | 1 of 3 | 69% + 28% |
| 7.1.4 | 1024, single | 376, 359, 344 ms | 1 of 3 | 18% + 28% |
| 9.1.6 | 256, multi | 325, 329, 302 ms | 1 of 3 | 663% + 48% |
| 9.1.6 | 512, single | 358, 360, 362 ms | 1 of 3 | 45% + 28% |

The runs without gaps matched XT's offline output at -74 to -77 dB. In the
runs with gaps CamillaDSP's log was clean, apart from one underrun in the
256 single setting. The gaps came from the standalone.

XT's figure for the same scenes is 0 samples and 45 to 65% of one core.

## Where the delay comes from

A second stream that only copied two channels from the cable's output back to
its input, with no processing, added 103 to 128 ms. That is the price of one
more program between the player and the device on this PC with this cable, and
it is most of what CamillaDSP adds. XT does not pay it, because the upmixer,
the convolver and the routing run inside the block Windows hands to the APO.

## Limits of these numbers

- One PC, one virtual cable, VB-CABLE's settings as they were. A cable with
  smaller internal buffers would lower every figure except XT's.
- The share of the cable inside those 103 to 128 ms was not separated from the
  relaying stream's own buffers.
- CamillaDSP's rate adjustment was off, because input and output were the same
  cable and share a clock. Playing to a real DAC needs it on, with its
  resampler.
- The standalone was measured in shared mode only. Exclusive mode and ASIO
  would allow smaller buffers and could not be used here, since the measuring
  player has to share the device.
- The 7.1 scene was not measured behind the standalone, because the
  standalone's output layout could not be changed without operating its window.
  The MultiChannel Plugin Host was not measured for the same reason.
- CamillaDSP rows without the standalone are one 20-second run each.
- zAuro runs on one thread in every host. Its cost is in XT's CPU figure and
  not in the CamillaDSP tables that exclude the upmixer.

## Independent run on a hosted runner

The offline part runs in GitHub Actions on a 2-core hosted runner, with the
private inputs fetched for the run and never uploaded
(`ci/latency-benchmark` branch, run 44991945414). Realtime latency cannot be
judged there: the cable's own baseline moved by 20 ms between runs.

| Program | Block or chunk | Threads | Speed against real time |
|---|---:|---|---:|
| XT (zAuro and 22 convolutions) | 480 | adaptive | 6.2 to 8.5x |
| XT (zAuro and 22 convolutions) | 128 | adaptive | 6.3 to 6.8x |
| CamillaDSP (22 convolutions) | 1024 | single / multi | 5.3x / 11.5x |
| CamillaDSP (22 convolutions) | 512 | single / multi | 2.3x / 5.4x |
| CamillaDSP (22 convolutions) | 128 | single / multi | 0.49x / 1.24x |
| Omniphony 0.7.0 (BRIR render only) | - | default | 35 to 37x |

On that runner XT's output at block 128 was identical to block 480, and
CamillaDSP's output matched XT's at -77 dB with no offset at every chunk size.
A speed below 1x means the program cannot keep up in real time. CamillaDSP at
chunk 128 on one thread is there.

### Omniphony

Omniphony renders object and channel audio to a BRIR and has no upmixer, so it
was given the BRIR stage only and its figure does not include zAuro. It is
fast. Three things keep it out of the tables above.

- On Windows, version 0.7.0 takes input from files and has no live input, so
  the delay it adds to a playing stream could not be measured the same way.
- It accepts the BRIR as a SOFA file and processes it differently. Its output
  did not match XT's: the best fit had a gain of 0.32 and a residual of
  -17 dB.
- Its realtime output on the runner, timed from start-up, came 46 to 57 ms
  after the reference with dropouts flagged. A start-up figure on a 2-core
  runner says little about steady operation.
