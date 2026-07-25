# Phase & Time

Two of the commands EqualizerAPO-XT can apply change nothing about how loud
anything is. `Delay` moves the whole signal later; an all-pass filter moves some
frequencies later than others. Between them they cover driver alignment,
crossover phase correction and transient shaping.

Until now the Editor had no way to show either of them. Its analysis graph drew
magnitude in dB, and a filter that is flat by construction draws as a straight
line at 0 dB whatever it is set to. That is why the all-pass read as a filter
that does nothing.

## Seeing it

The analysis dock's control bar has a three-way switch:

| | What it shows |
| --- | --- |
| **Mag** | Magnitude, in dB. The default, and unchanged. |
| **Phase** | Unwrapped phase, in degrees. |
| **GD** | Group delay, in milliseconds. |

Switching between them does not re-measure anything. One analysis run produces a
complex response, and all three readings are derived from it, so the switch costs
no engine run and no FFT.

### Include base delay

Under **Phase** and **GD** a checkbox appears. It is off by default.

The analyzer strips the configuration's leading silence before it measures. That
is what makes a filter's own phase readable at all - without it, a configuration
containing a convolution would bury every filter under a ramp thousands of turns
deep. With the box off you are looking at the configuration with its bulk delay
removed.

Switch it on and that delay goes back into the reading. A configuration that is
nothing but `Delay: 10 ms` is a flat line at zero with the box off, and a group
delay of exactly 10 ms with it on.

### Where the line breaks

Phase has no value where the response is zero - inside a notch's null, or after
a cancellation like `Copy: L=L-L`. The graph breaks the line there rather than
drawing through it, because a reading taken from what is left is the transform's
own round-off and its angle is noise. Group delay breaks one bin wider, since it
is a difference between neighbouring bins and a difference taken across a hole
measures the hole.

### What this graph is not

It is the digital response of the EqualizerAPO configuration. It does not
measure speakers, headphones, the room, or a microphone.

## The all-pass filter

```
Filter: ON AP Fc 100 Hz Order 1
Filter: ON AP Fc 100 Hz Q 0.707 Order 2
Filter: ON AP Fc 100 Hz BW Oct 1 Order 2
```

|  | Total rotation | Phase at Fc | Width | Group delay at Fc |
| --- | --- | --- | --- | --- |
| `Order 1` | 180° | −90° | none | 1 / (2π·Fc) |
| `Order 2` | 360° | −180° | `Q` or `BW Oct` | 2Q / (π·Fc) |

`Order` is optional and defaults to 2, so configurations written before it
existed are unaffected.

A 1st-order section is not a 2nd-order one at some particular setting. A
2nd-order section turns a full circle at every Q, so none of them produce a 90°
crossing. Two 1st-order sections at one frequency are exactly a 2nd-order section
at `Q 0.5`; below that they separate onto two different frequencies.

### The card

An all-pass opens its own card rather than the general filter knobs. It has no
gain, so there is no gain control; the card says outright that the magnitude is
fixed at 0 dB, and offers to switch the analysis graph to Phase or Group delay.

The width can be written as a `Q` or as a bandwidth in octaves, and the card lets
you choose which. Whichever the line was written in is what it is saved as. This
used to be broken: an all-pass written as `BW Oct 1` came back as `Q 1`, a filter
with about a factor of √2 less group delay at Fc.

Switching the mode does convert the number, and that conversion is exact between
the two numbers but does not preserve the filter exactly. The engine's bandwidth
branch carries a factor the conversion does not; the difference is negligible at
low Fc and grows with it (roughly 0.3% at 1 kHz, 35% at 10 kHz). Peaking filters
have always behaved this way. If you do not touch the mode selector, nothing is
converted.

New all-pass filters are created at `Q 0.707`. Existing ones keep whatever they
were written with - nothing is migrated.
