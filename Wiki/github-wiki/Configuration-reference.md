# Configuration reference
This page describes the configuration file format and the commands EqualizerAPO-XT understands. It is meant for advanced users. For an introduction or troubleshooting, see the [user documentation](Documentation). XT keeps the original Equalizer APO syntax, so configurations written for upstream Equalizer APO work unchanged.
## Configuration file format
Configuration files are read line by line. Each meaningful line has the form:

```
Command: Parameters
```

Lines that do not name a supported command are inert text. That is how comments work — a line starting with `#` is simply not a recognised command — and unknown command names are ignored too. Command names are case-sensitive: `Preamp` is active, while `preamp` is not. Every filtering command — `Preamp`, `Filter`, `IIR`, `Delay`, `Copy`, `GraphicEQ`, both convolution commands, `VSTPlugin`, `Hilbert`, `Velvet`, `SubwooferRouting` and `LoudnessCorrection` — reports the file, line and reason for malformed input in `EqualizerAPO.log` and on the line itself in the Editor. Later lines still load. Only the control commands (`Include`, `Device`, `Stage`, `If`, `Eval`) report through their own messages.

Example:

```
Device: High Definition Audio Device Speakers; Benchmark
#All lines below will only be applied to the specified device and the benchmark application
Preamp: -6 db
Include: example.txt
Filter  1: ON  PK       Fc     50 Hz   Gain  -3.0 dB  Q 10.00
Filter  2: ON  PEQ      Fc     100 Hz  Gain   1.0 dB  BW Oct 0.167

Channel: L
#Additional preamp for left channel
Preamp: -5 dB
#Filters only for left channel
Include: demo.txt
Filter  1: ON  LS       Fc     300 Hz  Gain   5.0 dB

Channel: 2 C
#Filters for second(right) and center channel
Filter  1: ON  HP       Fc     30 Hz
Filter  2: ON  LPQ      Fc     10000 Hz  Q  0.400

Device: Microphone
#From here, the lines only apply to microphone devices
Filter: ON  NO       Fc     50 Hz
```

## Filtering commands
These commands change the audio on the currently selected channels directly.

### Preamp
**Syntax:** `Preamp: <gain> dB`

Sets a preamplification value in decibels. Positive values raise the level, negative values lower it, and zero is neutral. A negative preamp is commonly used when filters add positive gain, to keep the signal from clipping. When several preamps apply to the same channel, the effective preamp is their sum in dB.

```
Preamp: -6.5 dB
```

### LoudnessCorrection
**Syntax:** `LoudnessCorrection: State <0|1> ReferenceLevel <integer dB> ReferenceOffset <integer dB> [Attenuation <0..1>]`

Tracks the Windows master volume of the default multimedia playback device and changes a 75 Hz low shelf and a 10 kHz high shelf to compensate for level-dependent hearing. The effective reference is `ReferenceLevel - ReferenceOffset`. Below it, the command increases low- and high-frequency compensation and reduces headroom to accommodate the bass boost; at the reference it is neutral.

`ReferenceLevel` is the Windows master-volume level, in dB, that produces 75 dB at the listening position. The Editor's **Calibrate** dialog plays pink noise and helps set it with a sound-level meter. `ReferenceOffset` moves the effective reference without repeating calibration. `Attenuation` scales the correction from none (`0`) to full (`1`) and defaults to `1`. `State 0` bypasses the filter. After calibration, adjust listening volume with the Windows volume control so the command can follow it.

```
LoudnessCorrection: State 1 ReferenceLevel -20 ReferenceOffset 0 Attenuation 1.0
```

### Filter
**Syntax:**
```
Filter <n>: ON <Type> Fc <Frequency> Hz Gain <Gain value> dB Q <Q value>
Filter <n>: ON <Type> Fc <Frequency> Hz Gain <Gain value> dB BW Oct <Bandwidth value>
```

Adds a filter of the given type, frequency, gain and Q / bandwidth. The first form (with Q) matches Room EQ Wizard's "Generic" equalizer type; the second (with bandwidth) matches "FBQ2496". The filter number `<n>` is not interpreted and may be omitted.

The table below lists the supported filter types. In the parameter columns, **X** marks a required parameter and **O** an optional one. These cover every filter of the "Generic" and "FBQ2496" types; other equalizer types may also work if their text format matches. Note one difference: the band-pass filter here is a true band-pass with no gain (like the low/high-pass filters), unlike the DCX2496's "BP", which is really a peaking filter.

Matching those export formats is why the table exists, but it is not what the
table is limited to. The all-pass in particular is a phase tool with its own
section below, not a row kept here for an importer's sake.

| Type | Description | Fc | Gain | Q/BW | Example |
| --- | --- | --- | --- | --- | --- |
| PK<br>Modal<br>PEQ | Peaking filter (parametric EQ) | X | X | X | `ON PK Fc 50.0 Hz Gain -10.0 dB Q 2.50`<br>`ON Modal Fc 100 Hz Gain 3.0 dB Q 5.41 T60 target 100 ms`<br>`ON PEQ Fc 100 Hz Gain 1.0 dB BW Oct 0.167` |
| LP<br>LPQ | Low-pass filter | X |  | O | `ON LP Fc 8000 Hz`<br>`ON LPQ Fc 10000 Hz Q 0.400` |
| HP<br>HPQ | High-pass filter | X |  | O | `ON HP Fc 30.0 Hz`<br>`ON HPQ Fc 20.0 Hz Q 0.500` |
| BP | Band-pass filter (not from DCX2496) | X |  | O | `ON BP Fc 1000 Hz Q 0.100` |
| LS<br>LSC *x* dB | Low-shelf filter (with centre freq.; *x* dB per octave for LSC) | X | X | O (1.2.1) | `ON LS Fc 300 Hz Gain 5.0 dB`<br>`ON LSC 10.8 dB Fc 300 Hz Gain 5.0 dB`<br>`ON LSC Fc 300 Hz Gain 5.0 dB Q 0.6473` |
| HS<br>HSC *x* dB | High-shelf filter (with centre freq.; *x* dB per octave for HSC) | X | X | O (1.2.1) | `ON HS Fc 1000 Hz Gain -3.0 dB`<br>`ON HSC 6 dB Fc 100 Hz Gain -6.0 dB`<br>`ON HSC Fc 100 Hz Gain -6.0 dB Q 0.4272` |
| LS 6dB<br>LS 12dB | Low-shelf filter (6 / 12 dB per octave, corner freq.) | X | X |  | `ON LS 6dB Fc 50.0 Hz Gain 7.2 dB`<br>`ON LS 12dB Fc 2000 Hz Gain -5.0 dB` |
| HS 6dB<br>HS 12dB | High-shelf filter (6 / 12 dB per octave, corner freq.) | X | X |  | `ON HS 6dB Fc 12000 Hz Gain 10.0 dB`<br>`ON HS 12dB Fc 500 Hz Gain 5.0 dB` |
| NO | Notch filter | X |  | O | `ON NO Fc 800 Hz` |
| AP | All-pass filter (see below) | X |  | X for `Order 2` | `ON AP Fc 900 Hz Q 0.707`<br>`ON AP Fc 900 Hz Order 1` |

#### All-pass

The all-pass is in this table because it is written the same way as the filters
around it, but it does something none of them do. It does not change level at
any frequency. What it changes is *when* each frequency arrives: its phase and
its group delay. That is what it is for - aligning drivers, correcting the phase
a crossover introduced, shaping a transient.

Because it is flat, a magnitude plot cannot show it. The Editor's analysis graph
switches between **Mag**, **Phase** and **GD** for exactly this reason; on
Magnitude an all-pass is a straight line no matter what it is set to.

```
Filter <n>: ON AP Fc <frequency> Hz Order 1
Filter <n>: ON AP Fc <frequency> Hz Q <q> Order 2
Filter <n>: ON AP Fc <frequency> Hz BW Oct <bandwidth> Order 2
```

`Order` chooses the section. It is optional, and a line without it is a
2nd-order section - so every configuration written before `Order` existed
behaves exactly as it did.

|  | Total rotation | Phase at Fc | Width | Group delay at Fc |
| --- | --- | --- | --- | --- |
| `Order 1` | 180° | −90° | none | 1 / (2π·Fc) |
| `Order 2` | 360° | −180° | `Q` or `BW Oct` | 2Q / (π·Fc) |

The two group-delay figures hold while Fc is well below Nyquist; the exact
2nd-order value is 4Q / sin(2π·Fc/rate) samples.

A 1st-order section is not a 2nd-order one with some particular Q. A 2nd-order
section always turns a full circle, so no setting of it produces a 90° crossing.
Two 1st-order sections at the same frequency are exactly a 2nd-order section at
`Q 0.5`, and below that they separate onto two different frequencies.

`Order 1` takes no width; a `Q` or `BW Oct` on such a line is ignored. `Order 2`
requires one. An order that is neither 1 nor 2 is an error.

The Editor creates a new all-pass at `Q 0.707` and leaves an existing one at
whatever it was written with. The width form the line was written in (`Q` or
`BW Oct`) is the form it is saved in. Switching the card between the two forms
converts the number exactly, but the engine's bandwidth branch carries a factor
the conversion does not, so the filter itself is not preserved exactly: the
difference is negligible at low Fc and grows with it (about 0.3 % at 1 kHz,
35 % at 10 kHz), the same as peaking filters have always behaved. The all-pass
opens its own card and offers to switch the analysis graph to Phase or GD.


### Filter with custom coefficients
**Syntax:** `Filter <n>: ON IIR Order <m> Coefficients <b0> <b1> ... <bm> <a0> <a1> ... <am>`

Adds a generic IIR filter of the given order and coefficients. The number of coefficients must be `2·(order+1)`. The transfer function is shown below.

<img src="Transfer-function.png"><br><em>IIR transfer function</em>

Because the coefficients usually depend on the sample rate, combine this with the [If](#if--elseif--else--endif) command or [inline expressions](#eval-and-inline-expressions) to supply the right values for the current rate. You *can* reproduce the BiQuad filters of the other types this way, but it runs slower, so it is not advisable.

```
# A lowpass biquad filter with Fc 3000 Hz for the sample rate 48 kHz
Filter: ON IIR Order 2 Coefficients 0.0380602 0.0761205 0.0380602 1.2706 -1.84776 0.729402
```

### Delay
**Syntax:**
```
Delay: <t> ms
Delay: <n> samples
```

Delays the selected channels by `t` milliseconds or `n` samples. Prefer milliseconds — they give the same delay regardless of sample rate.

```
# Delays the audio by 50.5 ms independent of sample rate
Delay: 50.5 ms
# Delays the audio by 480 samples (10 ms at 48 kHz)
Delay: 480 samples
```

### Copy
**Syntax:**
```
Copy: <Target channel>=<Factor>*<Source channel>+...
Copy: <Target channel>=<Source channel>+...
Copy: <Target channel>=<Constant value>+...
```

Replaces the target channel with the sum of the listed source channels, each with an optional factor. To add to the target instead of replacing it, include the target as one of the sources. A factor may be given in dB by appending `dB`. Several assignments can share one line if separated by spaces, so a single assignment must not contain spaces. A bare `0` is a constant; every other constant must contain a decimal point to distinguish it from a numeric channel index. Thus `L=2` copies channel 2, while `L=2.0` writes the constant 2. See the [Channel](#channel) command for channel identifiers.

```
# Adds the audio on channel R multiplied by 0.5 to channel L
Copy: L=L+0.5*R
# Replaces L by R plus C attenuated by 6 dB
Copy: L=R+-6dB*C
# Replaces channel 1 by the audio previously on R, and sets R to the constant 0.5
Copy: 1=R R=0.5
# Attention: sets L to the audio on channel 2 (not the constant 2 — no decimal point)
Copy: L=2
# Replaces the LFE channel with the left channel while muting the rest of a 5.1 system
Copy: LFE=L L=0.0 R=0.0 C=0.0 RL=0.0 RR=0.0
```

### GraphicEQ
**Syntax:** `GraphicEQ: <Frequency> <Gain (dB)>; <Frequency> <Gain (dB)>; ...`

Adds a graphic equalizer with the given bands and gains. Gains are interpolated linearly across the logarithmic frequency axis (so the curve looks straight in a log view), and the response is flat outside the specified bands.

```
# A 15-band graphic equalizer with ISO bands
GraphicEQ: 25 6; 40 4.5; 63 3; 100 1.5; 160 0; 250 0; 400 0; 630 0; 1000 0; 1600 0; 2500 0; 4000 0; 6300 1.5; 10000 3; 16000 3
```

### Convolution
**Syntax:** `Convolution: <File name>`

Adds a convolver that processes the signal with the impulse response in the named file. The file must be one of the formats supported by [libsndfile](https://libsndfile.github.io/libsndfile/) (WAV, FLAC, OGG and others). If it has several channels, they are mapped round-robin onto the selected channels (a stereo file across four channels maps L→1, R→2, L→3, R→4). **The file's sample rate must match the device's sample rate** or the convolver cannot be created. Latency and CPU cost depend on the length and phase of the impulse response (linear-phase costs half the file length in latency; minimum-phase is lower but inconsistent). The file name is relative to the configuration file, may be quoted, and may contain environment variables such as `%USERPROFILE%`. Impulse responses kept inside the config folder (or a subfolder) trigger an automatic reload when changed, so edits apply immediately. EqualizerAPO-XT removes the original length cap on impulse responses. A convolver is built for the block length the audio engine locked the APO with; if a block of another length arrives it mutes rather than re-initialize on the audio thread, and says so in the log (the same diagnostic serves `MultiConvolution` and `Hilbert`). When an application switches an endpoint to a small period, the engine locks the APO again and the convolver follows.

```
# Convolve with a recorded impulse response for a reverberation effect
Convolution: church.wav
```

### MultiConvolution
**Syntax:** `MultiConvolution: <channel>=[<factor>*]<file channel>[+[<factor>*]<file channel>...] [<channel>=... ...] <multichannel impulse response>`
**Syntax:** `MultiConvolution: <channel> <multichannel impulse response>`

Adds a convolver for true-stereo, BRIR (Binaural Room Impulse Response) and crossfeed setups, where `Convolution`'s in-place 1:1 mapping is not enough. Each mapping convolves the named channel's own signal with the listed channels of the single multichannel impulse-response file (0-based) and sums the results back into that channel. It compresses the Copy → Channel → Convolution → Copy fan-out/sum pattern into one line and is independent of the `Channel:` command: the channel selection neither feeds it nor is changed by it. All mappings read the signal as it was before the line, like `Copy:`, so `L=0+1 R=2+3` processes each ear with its own pair of impulse responses. The one-token form means "every channel of the file": with a 2-channel file, `MultiConvolution: L brir.wav` equals `MultiConvolution: L=0+1 brir.wav`. Whitespace around `=` and `+` is accepted.

Each file channel may carry a factor with `Copy:`'s grammar: `L=0.5*0+1` halves file channel 0's convolution result before the sum, `-1` inverts the phase, `-0.5` does both, and a dB value such as `-6dB*0` attenuates by that amount. Without a factor the result is used as-is.

A mapping target may name a channel that does not exist yet; it is created as a virtual channel (silent unless an earlier line wrote to it). A file-channel index beyond the file's channel count contributes silence and leaves a note in the log. The same path and sample-rate rules as `Convolution` apply: the file name is relative to the configuration file, may be quoted, may contain environment variables such as `%USERPROFILE%`, must be in a format supported by libsndfile, and its sample rate must match the device's. A bad path, sample-rate mismatch, or unusable file still creates the mapped channels, just silent, so later channel selections do not shift. In the Editor, the card shows the file next to a channel-mapping view in every skin: the sources are the file's channels, the outputs are the config's channels plus any virtual channel you add.

```
# A 4-channel BRIR: each ear gets its own pair of impulse responses
MultiConvolution: L=0+1 R=2+3 brir.wav
```

When an ear also needs the other speaker's signal (a full crossfeed matrix), copy it into a helper channel first; the helper's own signal is then the right input:

```
# Full crossfeed: XL/XR carry the opposite speaker's signal
Copy: XL=R XR=L
MultiConvolution: L=0 XL=1 XR=2 R=3 brir.wav
Copy: L=L+XL R=R+XR
```

### VSTPlugin bus layouts
**Syntax:** `VSTPlugin: Library "<plug-in path>" [ChunkData "<state>" | <parameter name or ID> <value> ...]`
**With explicit VST3 buses:** `VSTPlugin: Library "<plug-in path>" Input <layout> Output <layout> [ChunkData "<state>" | <parameter name or ID> <value> ...]`
**With per-slot channel fill:** `VSTPlugin: Library "<plug-in path>" Input <layout> [InputChannels <ch,ch,...>] Output <layout> [OutputChannels <ch,ch,...>] ...`

Loads a 64-bit VST2 (`.dll`) or VST3 (`.vst3`) plug-in and processes the currently selected channels. A relative library path is resolved under EqualizerAPO-XT's `VSTPlugins` folder; an absolute path may point elsewhere. The audio service reads the plug-in with the LOCAL SERVICE account's rights, so a library under a user profile (Desktop, Downloads) never loads during playback even though the Editor can open it; the VST card says so from the path alone and offers **Import**, which copies the library into the config directory, a location the audio service can always read and one that survives updates. The Editor can choose the module (a `.vst3` bundle directory can be picked directly), open or embed its panel, and save its state; while a panel is embedded the host keeps the plug-in in its processing state for the whole session and the button reads **Close panel**, and moving a control applies it to the engine at once unless auto-apply was turned off. State is written either as quoted `ChunkData` or as numeric parameter key/value pairs, not both in one canonical line.

`Input` and `Output` add an explicit main-bus contract to the existing `VSTPlugin` command. Use them for VST3 upmixers, downmixers, height expanders, and other plug-ins whose input and output layouts differ. If either key is present, both are mandatory; write `Auto` explicitly when one direction should use the normal host negotiation.

Supported layouts are `Auto`, `Mono`, `Stereo`, `4.0`, `4.1`, `5.0`, `5.1`, `6.1`, `7.1`, `7.1.2`, and `7.1.4`. Where VST3 defines multiple arrangements for the same logical layout, the host proposes the Windows-like Music arrangement first and then the matching Cine alternative. It may change the arrangement within that logical layout, but never the channel count of an explicitly selected direction.

```
# Stereo upmixer feeding an eight-channel device
VSTPlugin: Library "C:\Program Files\Common Files\VST3\Upmixer.vst3" Input Stereo Output 7.1

# Let the plug-in/device negotiate the input, but require a 7.1.4 output bus
VSTPlugin: Library "C:\Program Files\Common Files\VST3\Height Expander.vst3" Input Auto Output 7.1.4
```

If a VST3 plug-in rejects the contract, reports a different arrangement or bus width, or supplies invalid metadata, the filter is disabled and every input channel passes through unchanged. It does not retry with a different explicit width or create repeated stereo instances. When the loaded module is VST2, the backend ignores `Input` and `Output` and continues with ordinary VST2 processing.

`InputChannels` and `OutputChannels` choose which configuration channels occupy the negotiated slots. Without them, an explicit contract consumes the first channels in device order: `Input 5.1` reads channels 1–6 and `Output 5.1` writes channels 1–6. With a fill list, each negotiated slot names its channel explicitly, one entry per slot in the layout's channel order (for `5.1`: L, R, C, LFE, RL, RR). Entries accept the same channel names, aliases and 1-based numbers as `Copy`; `-` makes an input slot silent or discards an output slot. Each list requires an explicit (non-`Auto`) layout on its side, and its length must equal that layout's channel count. One channel may feed several input slots, but two output slots cannot write the same channel.

With a fill present, reading a channel into an input slot does not consume it: every channel that no output slot targets passes through unchanged, and nothing is zero-filled. If a fill names a channel the device does not have, the filter logs the reason, is disabled, and passes all audio through, like a rejected contract.

```
# Process the side pair through a stereo-only plug-in; everything else passes through
VSTPlugin: Library "C:\Program Files\Common Files\VST3\StereoVerb.vst3" Input Stereo InputChannels SL,SR Output Stereo OutputChannels SL,SR

# Upmix the rear pair to a 7.1 bed, keep the plug-in's LFE out of the device LFE
VSTPlugin: Library "C:\Program Files\Common Files\VST3\Upmixer.vst3" Input Stereo InputChannels RL,RR Output 7.1 OutputChannels 1,2,3,-,5,6,7,8
```

The older `StereoInput 1` spelling remains accepted. With no `Input`/`Output` pair and a stream wider than stereo, it marks a VST3 as an upmixer and asks the legacy automatic path for two input channels and the full device-width output. The card-based Editor maps that intent to `Input Stereo Output Auto`, shows a migration notice, and writes the explicit pair on its next save. From that save onward the strict contract behavior above applies: one contract instance, with unchanged passthrough if the pair is rejected. **Legacy rows** mode instead preserves `StereoInput 1` verbatim. Do not combine `StereoInput` with `Input`/`Output`. Existing `ChunkData` and parameter state keep their normal `VSTPlugin` key/value form.

The card-based Editor now provides **Input** and **Output** selectors beside the plug-in identity. For VST3 it probes the same negotiation as the audio engine, displays the actual pair when either side is `Auto`, and reports rejection as unchanged passthrough. The selectors lock while the plug-in is missing, loaded as VST2, or its panel is embedded. If a line carries layouts but the module loads as VST2, use **Options → Remove Input/Output layouts** to return to ordinary automatic negotiation.

**Legacy rows** mode has plain **Input** and **Output** dropdowns for the same contract. Setting both back to `Auto` removes the keys from the line, and an explicit pair disables the legacy Stereo input toggle (the two spellings cannot be combined).

The channel fill has its own editor in both modes: two rails inside the card (the input rail under the header, the output rail under the body; plain combo rows in Legacy rows mode), one dropdown per negotiated slot. A side with an `Auto` layout has no rail. The dropdowns offer exactly the channels selected at that line - a `Channel:` line above restricts them, and channels created by `Copy:` appear once a `Channel:` line selects them - plus the `-` silence/discard entry; a saved channel outside the current selection stays visible in the danger color, because the engine would disable the plug-in over it. When both rails exist, a fill switch on the input rail folds them away to save space (a card with a single rail does not fold). The first edit on a side writes the full list into the line; **Options → Remove channel fill** returns to the implicit first-channels default, which is not the same thing as an identity list (an explicit fill changes untargeted channels to passthrough). A side's list also drops automatically when that side's layout changes.

### Hilbert
**Syntax:** `Hilbert: [Shift=<channel>[,<channel>...]] [Align=<channel>[,<channel>...]] [Direction=-90|+90]`

Applies a fixed 1025-tap linear-phase Hilbert transform to the channels named by `Shift`. `Direction` selects a -90° (default) or +90° phase shift. The transform has 512 samples of latency and a unity-gain mid-band response. `Shift=ALL` is the default and addresses every channel that exists at this line.

`Align` applies only the matching 512-sample delay to explicitly named channels, without phase shifting them. A channel cannot appear in both roles, `Align=ALL` is not accepted, and `Align` cannot be used together with `Shift=ALL`. Channel names in this command are explicit and do not depend on the current `Channel:` selection. Unknown names are ignored and reported in the log.

```
Hilbert: Shift=SL,SR Align=L,R Direction=-90
```

The Editor exposes the two channel roles separately and plots the phase and group-delay response of the transform.

### Velvet
**Syntax:** `Velvet: [Mode=Dynamic|Static] [Amount=0%..100%] [Length=1ms..100ms] [Density=100/s..4000/s] [Evolution=0.1s..60s] [Transition=1ms..2000ms] [Decay=-120dB..0dB] [Variation=1..4294967295]`

Runs an independently generated sparse velvet-noise FIR on every currently selected channel. It decorrelates input audio; it does not add a continuous noise signal. `Amount` is the dry/wet mix, `Length` is the FIR time spread, `Density` is the average impulse density, `Decay` sets the end-of-window gain, and `Variation` is the deterministic seed stored in the configuration.

`Mode=Dynamic` is the default. A new deterministic bank is generated every `Evolution` interval and replaces the active bank through an equal-power `Transition`; the transition may not exceed 90% of the evolution interval. `Mode=Static` keeps one bank. Each channel receives a separately generated, unit-energy sparse response, and processing performs no allocation after initialization.

```
Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136
```

The Editor keeps the primary controls compact, reveals dynamic controls only when they apply, and puts density, transition, decay and variation in an Advanced fold. The former impulse-tap preview and correlation readout have been removed. Frequency-response analysis freezes a dynamic filter to its deterministic static snapshot and labels the graph accordingly, so an evolving response is never presented as a stationary measurement.

### SubwooferRouting
**Syntax:** `SubwooferRouting: State <compact JSON state>`
**Syntax:** `SubwooferRouting: Profile <profile file (*.swxt.json)>`

Runs a complete subwoofer-routing stage in one line: per-speaker-group crossovers, dedicated bass paths, preservation of the physical LFE input as its own source path, per-path gain/polarity/delay/EQ, and an output matrix that sums the processed paths back onto the physical channels. It replaces the `Copy` → `Channel` → `Filter` → `Copy` chains that such setups otherwise need. The command sees all channels regardless of the current `Channel:` selection, and channels the state does not reference pass through untouched.

The state is a JSON document with schema `equalizerapo.xt.subwoofer-routing`. `State` carries it inline; `Profile` reads the same JSON from a UTF-8 file, relative to the configuration file, optionally quoted, with environment variables such as `%USERPROFILE%` allowed. The state names physical inputs, mixes them into named main/bass/source-LFE paths (a source-LFE path reads the original LFE input even when the output matrix later rewrites the LFE output), processes each path, and mixes the paths onto the outputs. Headroom is either automatic (the compiled response is analyzed from 20 Hz to Nyquist and a common trim keeps the predicted peak at or below 0 dBFS) or a manual trim in dB.

```
SubwooferRouting: Profile "SubwooferRouting\Issue 246 Front Rear 4.1.swxt.json"
```

The built-in preset `Issue #246 - Front/Rear 4.1` reproduces the front/rear 4.1 configuration from issue #246: front mains high-passed at 80 Hz with a 4th-order 80 Hz front bass path (delayed 2.5 ms), rear mains at 100 Hz with a 60 Hz rear bass path (delayed 2.0 ms), the LFE input preserved as a source-LFE path at +10 dB, and a summing matrix with -14 dB feeds from the front-bass and source-LFE paths into the LFE output. Its output matches the original low-level filter chain sample for sample. The per-path EQ slots are empty; `Filter:` lines can be imported into them and become the state's biquad list (a profile never executes an `Include:`).

An invalid or unreadable state never mutes audio: the line reports its error in the log and the analysis panel, and the rest of the configuration keeps running. The Editor edits the same state as a card; writing changes back into a linked `Profile` file is not implemented yet, so editing such a row from the card turns it into an inline `State`. The standalone `EAPO XT Subwoofer Routing` VST3 plug-in ships in the installation's `VST3\` folder as `EapoXtSubwooferRouting.vst3` with its MIT `LICENSE`, runs the identical DSP core, exchanges the same JSON via its plug-in state (so presets move between the two unchanged), negotiates stereo through 7.1 layouts including 4.1, exposes bypass, source-LFE gain, polarity and delay, and the headroom trim as host-automatable parameters, and has no custom editor: hosts show their generic parameter view.

## Control commands
These do not change the audio directly; they control which commands run and how they apply.

### Include
**Syntax:** `Include: <File name>`

Loads the named file as a configuration file. Splitting the actual filter definitions into a separate file (rather than editing `config.txt` directly) lets you, for example, set a preamp before pulling them in. A relative path is resolved against the including file and is no longer cut at 260 characters.

```
Include: example.txt
```

### Device
**Syntax:** `Device: <Device pattern 1>; <Device pattern 2>; ...`

Matches the pattern against the connection name, device name and GUID of the current device: a playback endpoint, a recording endpoint, or an ASIO stream, whose string is `ASIO <driver name> {driver CLSID}` (so `Device: ASIO` selects every ASIO stream and `Device: Topping` one interface; an endpoint offered in exclusive mode keeps the endpoint's own name and GUID, so `Device: {endpoint GUID}` matches its APO and its ASIO entry alike). Without a `Device:` line a filter applies to endpoints and ASIO streams alike. If it does not match, every following command except another `Device` is ignored. A pattern is a set of space-separated words that must all appear in the string "*Device_name Connection_name GUID*". Separate alternative patterns with `;`, of which one must match. The special pattern `all` always matches. The benchmark application reports device name "Benchmark" and connection "File output". The easiest way to produce a correct `Device` line is the button in the Device Selector.

```
# Matches "High Definition Audio Device" with connection "Speakers",
# and also "Benchmark" used by the benchmark application
Device: High Definition Audio Device Speakers; Benchmark
```

### Channel
**Syntax:** `Channel: <Channel position 1> <Channel position 2> ...`

Selects the channels that subsequent ordinary filtering commands apply to, including `Preamp`, `Filter`, `Delay`, `GraphicEQ`, `Convolution`, `VSTPlugin`, `Velvet` and `LoudnessCorrection`. Commands that name their own channels — `Copy`, `MultiConvolution`, `Hilbert` and `SubwooferRouting` — follow their own rules instead. Positions may be given by identifier (a 1–3 letter acronym) or by number (counted from 1). The supported configurations are listed below; for an unsupported configuration, channels can only be selected by number. Separate several channels with spaces. The special position `all` selects every channel. An ASIO stream names its channels from the channel count the same way (2 → `L R`, 6 → 5.1, 8 → 7.1, otherwise numbers), and the engine sees every physical channel of the interface, so the names do not move when the application opens only some of them.

| Configuration | L | R | C | LFE | RL | RR | RC | SL | SR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Mono |  |  | 1 |  |  |  |  |  |  |
| Stereo | 1 | 2 |  |  |  |  |  |  |  |
| Quadraphonic | 1 | 2 |  |  | 3 | 4 |  |  |  |
| Surround | 1 | 2 | 3 |  |  |  | 4 |  |  |
| 5.1 Surround | 1 | 2 | 3 | 4 |  |  |  | 5 | 6 |
| 5.1 Surround (alt.) | 1 | 2 | 3 | 4 | 5 | 6 |  |  |  |
| 7.1 Surround | 1 | 2 | 3 | 4 | 5 | 6 |  | 7 | 8 |


**Attention:** it is tempting to send low-frequency filters only to the LFE channel, but this often does not work as expected. Many systems apply bass redirection *after* Equalizer APO, so LFE-only filters miss the redirected sound; and because crossover filters fade in gradually, low frequencies may play across several speakers, not just the subwoofer. **Apply low-frequency filters to all channels** to avoid this.

```
# Selects the left channel and the rear left channel
Channel: L RL
# Selects the first, second and center channel
Channel: 1 2 C
```

### Stage
**Syntax:** `Stage: <stage 1> <stage 2>`

Chooses which stage(s) the following filtering commands run on. Output devices have two stages, **pre-mix** and **post-mix**; input devices have only **capture**. An ASIO stream has no mix: `Stage: capture` blocks apply to its input direction and everything else to its output direction.

<img src="Stages.png" width="500"><br><em>Pre-mix and post-mix stages of an output device</em>

The selected stages start as post-mix and capture, so filtering runs exactly once for output and input devices. Post-mix is preferred over pre-mix because pre-mix filtering happens per-application and costs more CPU. Pre-mix is needed for specific tasks such as upmixing, where filtering must depend on the number of input channels.

```
Stage: pre-mix
# do upmixing
If: inputChannelCount == 2
# note that there may be audio on SL, SR from another APO
Copy: SL=SL+L SR=SR+R
EndIf:
# ...
Stage: post-mix
# do room correction
# ...
```

## Expression commands
These commands use expressions to vary the processing at runtime. Expressions are a tiny embedded language whose syntax is closer to scripting languages, built from constants, variables, operators and functions.

**Constants:**

| Name | Description |
| --- | --- |
| e, pi | Mathematical constants |
| inputChannelCount | Number of channels input to the current APO stage |
| outputChannelCount | Number of channels output from the current APO stage |
| sampleRate | Sample rate (Hz) of the audio being processed |
| deviceName | Name of the audio device |
| connectionName | Name of the connection on the audio device |
| deviceGuid | GUID of the audio device endpoint |
| stage | Current APO stage (pre-mix, post-mix or capture) |


**Variables:** user-defined variables, valid while the configuration loads, are created with the assignment operator (`=`). They are not kept between reloads; they only carry temporary values for use later in the same file.

**Operators:**

| Name | Description |
| --- | --- |
| `+ - * / ^` | Arithmetic operators |
| `= += -= *= /=` | Assignment operators |
| `and or xor` | Logical operators |
| `== != < > <= >=` | Comparison operators |
| `& \| << >>` | Bit-wise / bit-shift operators |
| `+` | String concatenation (at least one operand must be a string) |
| `(float) (int)` | Type conversion (from a numeric type) |
| `condition ? then : else` | Conditional (ternary) operator |
| `{1,2}` | Array creation |
| `array[0]` | Array access |


**Functions:**

| Name | Description |
| --- | --- |
| abs | Absolute value |
| sin, cos, tan,<br>sinh, cosh, tanh | Trigonometric functions |
| ln, log, log10, exp | Logarithmic / exponential functions |
| sqrt | Square root |
| min, max, sum | Minimum / maximum / sum of arguments |
| str2dbl | String to float |
| strlen | Length of a string |
| tolower, toupper | Lower / upper case conversion |
| sizeof | Length of an array |
| regexSearch | First match of a regular expression (first argument) in a string (second). Empty if no match; otherwise an array whose first value is the whole match and whose further values are capturing groups. |
| regexReplace | Replaces every match of a regular expression (first argument) in a string (second) with a string (third). Returns the result string. |
| readRegString | Reads a string value (second argument) from a registry key (first). Monitors the key and reloads on change. |
| readRegDWORD | Reads an integer value (second argument) from a registry key (first). Monitors the key and reloads on change. |


The expression types are string, boolean, int, float and matrix/array (abbreviated *s*, *b*, *i*, *f*, *m* in error messages). String constants use double quotes; backslashes and quotes inside them are escaped with a backslash. Booleans are `true` / `false`. Numbers use no thousands separator and a point for the decimal. Arrays use the `{}` operator. Combine several expressions with semicolons; the result is the value of the last one.

```
# User-defined variables
Eval: a=0; b=pi
# Comparison
Eval: a > b ? "a is larger" : "b is larger"
# Trigonometric functions and the exponentiation operator
Eval: sin(a)^2 + cos(a)^2 == 1
# Matching the device name with a regular expression
Eval: a=regexSearch("High Definition .*", deviceName); sizeof(a) > 0 ? "found" : "not found"
# Reading the configuration path from the registry
Eval: readRegString("HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO", "ConfigPath")
```

### If / ElseIf / Else / EndIf
**Syntax:**
```
If: <expression>
ElseIf: <expression>
Else:
EndIf:
```

Runs the commands between `If` and `EndIf` conditionally, like an if/else statement. `If` evaluates its expression as a boolean and runs the following commands when it is true. `ElseIf` after a branch that was already true is skipped; otherwise its expression is evaluated and its block runs when true. `Else` behaves like an `ElseIf` that is always true. `EndIf` ends the block. Conditionals can be nested, and `ElseIf`/`Else` always bind to the nearest open `If`. Lines may be indented for readability. For technical reasons, `If` cannot be used to run `Device` commands conditionally, because `Device` has higher priority.

```
If: sampleRate == 44100
	...
ElseIf: sampleRate == 48000
	...
	If: inputChannelCount == 1
		...
	ElseIf: inputChannelCount == 2
		...
	EndIf:
Else:
	...
EndIf:
```

### Eval and inline expressions
**Syntax:**
```
Eval: <expression>
<Command>: ... `<expression>` ...
```

`Eval` evaluates an expression and discards the result; it is mainly for defining variables or testing. An **inline expression** embeds a result into a command's parameter string: everything from the first backtick to the second (inclusive) is replaced by the expression's value converted to a string. Inline expressions cannot be used in `Device` or `If`/`ElseIf` commands, but they can be used inside `Eval`.

```
# Specify gain linearly
Eval: linGain = 0.5
Filter: ON PK Fc 1000 Hz Gain `20*log10(linGain)` dB Q 10.0
```

## See also
* [Documentation](Documentation) — installation, first configuration and troubleshooting.
* [Developer documentation](Developer-documentation) — building XT and writing your own APO.
* [이 문서의 한국어판 (Korean)](Korean-Configuration-reference)
