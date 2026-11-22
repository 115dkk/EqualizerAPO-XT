"""How much one more pass through the cable costs by itself: a second, low-latency stream copies
ch 15/16 from the cable's output back to ch 1/2 of its input, with no processing."""
import json, os, sys, time
import numpy as np, sounddevice as sd, soundfile as sf
from scipy.signal import fftconvolve
W = sys.argv[1]; R = 48000; SEG = 15 * R; LEAD = 6 * R
music, _ = sf.read(os.path.join(W, 'music48.wav'), dtype='float32', always_2d=True)
def device(name, want_input):
    wasapi = [i for i, a in enumerate(sd.query_hostapis()) if a['name'] == 'Windows WASAPI'][0]
    return [i for i, d in enumerate(sd.query_devices()) if d['hostapi'] == wasapi and d['name'] == name
            and (d['max_input_channels'] if want_input else d['max_output_channels']) == 16][0]
i_dev, o_dev = device('CABLE Output(VB-Audio Virtual Cable)', True), device('CABLE In 16ch(VB-Audio Virtual Cable)', False)
total = LEAD + SEG + 2 * R
out = np.zeros((total, 16), dtype=np.float32); out[LEAD:LEAD + SEG, 14:16] = music[:SEG]
def lag(x, ref):
    piece = x[LEAD - R // 2:LEAD + SEG + 2 * R].astype(float); core = ref[4 * R:12 * R].astype(float)
    c = fftconvolve(piece, core[::-1], mode='valid'); k = int(np.argmax(np.abs(c)))
    got = piece[k:k + core.size]; g = np.dot(got, core) / np.dot(core, core); res = got - g * core
    return LEAD - R // 2 + k - (LEAD + 4 * R), round(float(10 * np.log10(np.sum(res ** 2) / np.sum((g * core) ** 2) + 1e-30)), 1)
for latency in json.loads(sys.argv[2]):
    rec = np.zeros((total, 16), dtype=np.float32); pos = {'o': 0, 'i': 0, 's': 0, 'p': 0}
    def cb(indata, outdata, frames, t, status):
        o = pos['o']; n = min(frames, total - o); outdata[:n] = out[o:o + n]; outdata[n:] = 0; pos['o'] = o + n
        i = pos['i']; k = min(frames, total - i); rec[i:i + k] = indata[:k]; pos['i'] = i + k
        if n < frames: raise sd.CallbackStop
    def loop(indata, outdata, frames, t, status):
        if status: pos['p'] += 1
        outdata[:] = 0; outdata[:, 0:2] = indata[:, 14:16]
    with sd.Stream(samplerate=R, blocksize=0, latency='high', device=(i_dev, o_dev), channels=(16, 16), dtype='float32', callback=cb) as st:
        with sd.Stream(samplerate=R, blocksize=0, latency=latency, device=(i_dev, o_dev), channels=(16, 16), dtype='float32', callback=loop) as lp:
            rep = [round(x * 1000, 1) for x in lp.latency]
            while st.active: time.sleep(0.05)
    m = lag(rec[:, 14:16].mean(axis=1), music[:SEG].mean(axis=1)); x = lag(rec[:, 0:2].mean(axis=1), music[:SEG].mean(axis=1))
    print(json.dumps({'loop_latency_setting': latency, 'loop_reported_ms': rep, 'added_samples': x[0] - m[0], 'added_ms': round((x[0] - m[0]) / 48, 2), 'residual_db': x[1], 'loop_status_events': pos['p']}))
