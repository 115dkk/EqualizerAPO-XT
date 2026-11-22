"""The front end alone: music on ch 15/16 -> zAuro standalone -> ch 3..14, recorded in the same stream.
Reports which channels carry signal and how far each lags XT's own offline plugin output."""
import json, os, sys, time
import numpy as np
import psutil
import sounddevice as sd
import soundfile as sf
from scipy.signal import fftconvolve
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import front

WORK = sys.argv[1]
buffers = json.loads(sys.argv[2])
RATE = 48000
SEG = 20 * RATE
LEAD = 20 * RATE
music, _ = sf.read(os.path.join(WORK, 'music48.wav'), dtype='float32', always_2d=True)
feed, _ = sf.read(os.path.join(WORK, 'feed-s714-keep.wav'), dtype='float32', always_2d=True)


def device(name, want_input):
    wasapi = [i for i, a in enumerate(sd.query_hostapis()) if a['name'] == 'Windows WASAPI'][0]
    hits = [i for i, d in enumerate(sd.query_devices()) if d['hostapi'] == wasapi and d['name'] == name
            and (d['max_input_channels'] if want_input else d['max_output_channels']) == 16]
    assert len(hits) == 1
    return hits[0]


def lag_of(recorded, reference, seg_start):
    lo = max(0, seg_start - RATE // 2)
    piece = recorded[lo:seg_start + SEG + 2 * RATE].astype(np.float64)
    core = reference[4 * RATE:16 * RATE].astype(np.float64)
    if np.sum(core ** 2) < 1e-9 or np.sum(piece ** 2) < 1e-9:
        return None
    corr = fftconvolve(piece, core[::-1], mode='valid')
    k = int(np.argmax(np.abs(corr)))
    got = piece[k:k + core.size]
    gain = float(np.dot(got, core) / np.dot(core, core))
    resid = got - gain * core
    db = 10 * np.log10(np.sum(resid ** 2) / max(np.sum((gain * core) ** 2), 1e-30) + 1e-30)
    win = RATE // 100
    blocks = resid[:resid.size // win * win].reshape(-1, win)
    refb = (gain * core)[:blocks.size].reshape(-1, win)
    ratio = np.sum(blocks ** 2, axis=1) / (np.sum(refb ** 2, axis=1) + 1e-12)
    spikes = int(np.sum((ratio > 0.01) & (ratio > 10 * np.median(ratio))))
    return [lo + k - (seg_start + 4 * RATE), round(gain, 3), round(float(db), 1), spikes]


total = LEAD + SEG + 3 * RATE
out = np.zeros((total, 16), dtype=np.float32)
out[LEAD:LEAD + SEG, 14:16] = music[:SEG]
done = []
for dtype, buffer in buffers:
    front.write_settings(0, buffer, dtype)
    rec = np.zeros((total, 16), dtype=np.float32)
    pos = {'o': 0, 'i': 0, 'status': 0}

    def callback(indata, outdata, frames, timeinfo, status):
        if status:
            pos['status'] += 1
        o = pos['o']
        n = min(frames, total - o)
        outdata[:n] = out[o:o + n]
        outdata[n:] = 0
        pos['o'] = o + n
        i = pos['i']
        k = min(frames, total - i)
        rec[i:i + k] = indata[:k]
        pos['i'] = i + k
        if n < frames:
            raise sd.CallbackStop

    f = None
    marks = {}
    try:
        with sd.Stream(samplerate=RATE, blocksize=0, latency='high', device=(device('CABLE Output(VB-Audio Virtual Cable)', True),
                       device('CABLE In 16ch(VB-Audio Virtual Cable)', False)), channels=(16, 16), dtype='float32', callback=callback) as stream:
            f = front.Front()
            p = psutil.Process(f.pid)
            while stream.active:
                for key, at in (('a', LEAD), ('b', LEAD + SEG)):
                    if key not in marks and pos['o'] >= at:
                        marks[key] = (time.time(), sum(p.cpu_times()[:2]))
                time.sleep(0.05)
    finally:
        graceful = f.close() if f else None
    saved = front.read_setup()
    marker = lag_of(rec[:, 14:16].mean(axis=1), music[:SEG].mean(axis=1), LEAD)
    row = {'type': dtype, 'buffer_asked': buffer, 'saved_setup': saved[0][saved[0].find('audioDeviceRate'):] if saved[0] else None,
           'closed_gracefully': graceful, 'callback_status': pos['status'], 'marker': marker,
           'rms': [round(float(np.sqrt(np.mean(rec[LEAD + RATE:LEAD + SEG, c] ** 2))), 4) for c in range(16)],
           'cpu_percent_of_one_core': round(100 * (marks['b'][1] - marks['a'][1]) / (marks['b'][0] - marks['a'][0]), 1)}
    row['channels_vs_xt_plugin_output'] = {}
    for c in range(2, 14):
        r = lag_of(rec[:, c], feed[:SEG, c], LEAD)
        if r:
            row['channels_vs_xt_plugin_output'][c + 1] = [r[0] - marker[0]] + r[1:]
    print(json.dumps(row))
    np.save(os.path.join(WORK, f'front_probe_rec_{len(done)}.npy'), rec[:, 2:5])
    if done:
        base = np.load(os.path.join(WORK, 'front_probe_rec_0.npy'))
        b0 = done[0]
        same = lag_of(rec[:, 2], base[LEAD + b0:LEAD + b0 + SEG, 0], LEAD)
        print(json.dumps({'vs_first_run_ch3': same}))
    done.append(marker[0] + row['channels_vs_xt_plugin_output'][3][0])
front.write_settings(1, 480)
