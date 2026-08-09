# The analysis response

How a configuration becomes the curve in the Editor's analysis dock, and why
the pieces are split where they are.

```
AnalysisThread            FilterEngine + FFTW, on a worker thread
        │  publishes shared_ptr<const AnalysisResponse>
        ▼
AnalysisResponse          complex bins, sample rate, stripped latency
        │  buildAnalysisCurve(response, request)
        ▼
AnalysisCurve             one value per pixel column, fitted range, ticks, labels
        │  buildCurveSegments(...) → QVector<QPolygonF>
        ▼
EqGraphView               geometry, cursor, hover; fills AnalysisGraphState
        │  ISkin::paintAnalysisGraph
        ▼
the active skin           every pixel
```

## Why the response is complex

The analyzer has always computed a complex spectrum. It used to convert it to
magnitude one step later and throw the phase away, which is why a configuration
that only shifts phase — an all-pass, a delay — drew as a straight line at 0 dB.

Keeping the complex numbers means one analysis answers all three readings.
Switching the graph between magnitude, phase and group delay re-derives the
curve from numbers already in hand: no `FilterEngine` run, no FFT, and nothing
that waits on the analysis thread.

## AnalysisResponse

`Editor/analysis/AnalysisResponse.h`. Qt-free, so the thread, the curve builder
and the tests can agree on one representation without dragging widgets into the
dependency.

It holds `fftSize / 2 + 1` bins, which is exactly what a real-to-complex
transform produces. It used to hold `fftSize`, and the publish step copied all
of them, so every run read the uninitialized half.

`latencyFrames` is the leading silence the analyzer stripped before
transforming. That strip is what makes a filter's own phase readable at all —
without it a configuration containing a convolution buries every filter under a
ramp thousands of turns deep. The bins therefore describe the configuration
with its bulk delay removed, and a view that wants the delay back reapplies the
linear phase this implies rather than asking for a second analysis.

## Publication

The thread builds the response outside the mutex — the copy out of the FFTW
buffer is unavoidable and the UI should not wait behind it — and publishes it as
a `shared_ptr<const>` under the lock. The object is never modified after
publication, so a reader takes a snapshot for the price of a refcount and can
drop the lock before building anything from it.

The pointer is never null. An unfinished or failed run publishes an empty
response rather than nothing, so the graph clears instead of keeping the
previous configuration's curve, and no caller needs a null check.

## ResponseCurveBuilder

`Editor/analysis/ResponseCurveBuilder.h`. Derives one metric.

Values are computed per bin first and interpolated per pixel column second.
That order is what makes phase and group delay possible at all: both are
defined across neighbouring bins, not at a single one, so neither can be
computed from a pixel's frequency alone.

**Magnitude** reproduces `GainCurveIterator`, the path it replaced, exactly — same
bracketing bins, same log-frequency parameter, same short circuit when both ends
are equal (which is how `-inf` used to survive), same `-120 dB` display floor.
A test runs both across all 905 pixel columns of the real graph width and
requires the same doubles, not a tolerance.

**Phase** is unwrapped. The raw argument sawtooths every half turn and a
2nd-order all-pass turns a full circle, so without unwrapping the one thing the
view exists to show would be shredded into stripes.

**Group delay** is differentiated from that same unwrapped series rather than
computed separately. That is what makes the two views agree about the base
delay: subtracting a linear phase adds exactly the latency to the derivative,
which is the relation the two modes are supposed to have.

### Holes

A bin whose magnitude falls below 1e-9 of the response's peak carries no phase
worth reading — what is left is the transform's own round-off, whose argument
wanders at random, and it is where an unguarded group delay explodes. Those bins
become holes. The curve breaks across them and the unwrap restarts on the far
side rather than carrying an offset it cannot know. Group delay goes missing one
bin wider than phase, because a difference taken across a hole measures the
hole.

A hole reaches the skins as a break between two entries of
`AnalysisGraphState::curves`. Joining them would claim the configuration passed
through readings it never had.

## What the skins are given

`AnalysisGraphState` carries finished strings, not numbers plus a unit: the two
ends of the value axis, a combined span, both ends of the frequency axis, and
the cursor readout. A skin prints them. The point is that adding a fourth
reading later does not mean editing five skins again.

`clipping` is only ever true for magnitude. A positive phase or a long group
delay is not a danger state.

`zeroVisible` says whether the metric's zero is inside the fitted range. The
older test — is `zeroY` inside `plotRect` — was always true, because the
value-to-y mapping clamps.

## The frequency axis

20 Hz to 20 kHz, capped at Nyquist. Above Nyquist there is no response, and the
previous path held the last bin's value across the rest of the axis, drawing a
flat line the configuration never produced. Only visible below a 40 kHz sample
rate.

## What is not here

The graph shows the digital response of the EqualizerAPO configuration. It does
not measure speakers, headphones, the room, or a microphone.
