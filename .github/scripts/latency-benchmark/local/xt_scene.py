"""One continuous duplex stream through the virtual cable for one scene: the music with
XT processing the endpoint, then with XT's section for that endpoint removed. Also reads
the audio engine process's CPU time over both segments. Temporary scene files are written
next to config.txt and removed afterwards; config.txt is restored. Prints numbers only."""
import hashlib, json, os, sys, time
import numpy as np
import psutil
import sounddevice as sd
import soundfile as sf
from scipy.signal import fftconvolve
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenes import Scene

WORK, ASSETS, CONFIG, SCENE = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
RENDER = 'CABLE In 16ch(VB-Audio Virtual Cable)'
CAPTURE = 'CABLE Output(VB-Audio Virtual Cable)'
RATE = 48000
SEG = 30 * RATE


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def device(name, want_input):
    wasapi = [i for i, a in enumerate(sd.query_hostapis()) if a['name'] == 'Windows WASAPI'][0]
    hits = [i for i, d in enumerate(sd.query_devices()) if d['hostapi'] == wasapi and d['name'] == name
            and (d['max_input_channels'] if want_input else d['max_output_channels']) == 16]
    if len(hits) != 1:
        raise SystemExit(f'device lookup failed for one endpoint ({len(hits)} matches)')
    return hits[0]


def audiodg_cpu():
    total = 0.0
    for p in psutil.process_iter():
        try:
            if p.name().lower() == 'audiodg.exe':
                total += sum(p.cpu_times()[:2])
        except psutil.Error:
            pass
    return total


music, _ = sf.read(os.path.join(WORK, 'music48.wav'), dtype='float32', always_2d=True)
ref, ref_rate = sf.read(os.path.join(WORK, f'ref-{SCENE}-keep.wav'), dtype='float32', always_2d=True)
assert ref_rate == RATE and music.shape[0] == SEG

LEAD = 10 * RATE
GAP = 12 * RATE
TAIL = 3 * RATE
seg1 = LEAD
seg2 = LEAD + SEG + GAP
total = seg2 + SEG + TAIL
out = np.zeros((total, 16), dtype=np.float32)
for start in (seg1, seg2):
    out[start:start + SEG, 0:2] = music
    out[start:start + SEG, 14:16] = music
rec = np.zeros((total, 16), dtype=np.float32)
pos = {'out': 0, 'in': 0, 'status': 0}


def callback(indata, outdata, frames, timeinfo, status):
    if status:
        pos['status'] += 1
    o = pos['out']
    n = min(frames, total - o)
    outdata[:n] = out[o:o + n]
    outdata[n:] = 0
    pos['out'] = o + n
    i = pos['in']
    k = min(frames, total - i)
    rec[i:i + k] = indata[:k]
    pos['in'] = i + k
    if n < frames:
        raise sd.CallbackStop


original = open(CONFIG, 'rb').read()
original_sha = hashlib.sha256(original).hexdigest()
lines = original.splitlines(keepends=True)
starts = [i for i, l in enumerate(lines) if l.lstrip().lower().startswith(b'device:')]
hits = [i for i in starts if b'CABLE In 16ch' in lines[i]]
if len(hits) != 1:
    raise SystemExit(f'config: expected one Device line for the cable endpoint, found {len(hits)}')
begin = hits[0]
later = [i for i in starts if i > begin]
end = later[0] if later else len(lines)
reduced = b''.join(lines[:begin] + lines[end:])
nl = bytes([13, 10]) if lines[begin].endswith(bytes([13, 10])) else bytes([10])
original.decode('utf-8')
scene = Scene(SCENE, ASSETS, WORK)
cfg_folder = os.path.dirname(CONFIG)
include_name = f'_bench_{SCENE}.txt'
temp_files = [os.path.join(cfg_folder, include_name), os.path.join(cfg_folder, scene.brir_name)]
section = ['Device: CABLE In 16ch VB-Audio Virtual Cable', 'Copy: L=1 R=2', 'Channel: L R', 'Include: ' + include_name, 'Copy: 1=L 2=R']
fixed = b''.join(lines[:begin] + [line.encode('utf-8') + nl for line in section] + lines[begin + 1:])
print(json.dumps({'scene': SCENE, 'config_sha_before': original_sha}))

swapped_at = None
cpu = {}
marks = (('a0', seg1 + 3 * RATE), ('a1', seg1 + SEG - RATE), ('b0', seg2 + 3 * RATE), ('b1', seg2 + SEG - RATE))
try:
    scene.write_brir(cfg_folder)
    open(temp_files[0], 'wb').write(('\r\n'.join(scene.chain_lines(True) if len(sys.argv) < 6 else scene.plugin_lines(True) + ['Copy: L=VFL R=VFR']) + '\r\n').encode('utf-8'))
    open(CONFIG, 'wb').write(fixed)
    time.sleep(8)
    stream = sd.Stream(samplerate=RATE, blocksize=0, latency='high', device=(device(CAPTURE, True), device(RENDER, False)),
                       channels=(16, 16), dtype='float32', callback=callback)
    with stream:
        while stream.active:
            if swapped_at is None and pos['out'] > seg1 + SEG + 2 * RATE:
                open(CONFIG, 'wb').write(reduced)
                swapped_at = pos['out']
            for key, at in marks:
                if key not in cpu and pos['out'] >= at:
                    cpu[key] = (time.perf_counter(), audiodg_cpu())
            time.sleep(0.02)
finally:
    open(CONFIG, 'wb').write(original)
    time.sleep(3)
    for f in temp_files:
        if os.path.exists(f):
            os.remove(f)
    print(json.dumps({'config_restored_sha_equal': sha(CONFIG) == original_sha,
                      'temp_files_left': [os.path.basename(f) for f in temp_files if os.path.exists(f)]}))


def lag_of(recorded, reference, seg_start):
    lo = max(0, seg_start - RATE // 2)
    piece = recorded[lo:seg_start + SEG + 2 * RATE]
    core = reference[5 * RATE:25 * RATE]
    corr = fftconvolve(piece, core[::-1], mode='valid')
    k = int(np.argmax(np.abs(corr)))
    lag = lo + k - (seg_start + 5 * RATE)
    got = piece[k:k + core.size]
    gain = float(np.dot(got, core) / np.dot(core, core))
    resid = got - gain * core
    resid_db = 10 * np.log10(np.sum(resid ** 2) / max(np.sum((gain * core) ** 2), 1e-30) + 1e-30)
    win = RATE // 100
    blocks = resid[:resid.size // win * win].reshape(-1, win)
    refb = (gain * core)[:blocks.size].reshape(-1, win)
    ratio = np.sum(blocks ** 2, axis=1) / (np.sum(refb ** 2, axis=1) + 1e-12)
    spikes = int(np.sum((ratio > 0.01) & (ratio > 10 * np.median(ratio))))
    return {'lag': int(lag), 'gain': round(gain, 4), 'residual_db': round(float(resid_db), 2), 'spikes': spikes}


mono = lambda x: x.mean(axis=1).astype(np.float64)
music_m = mono(music)
ref_m = mono(ref[:SEG, 0:2])
pct = lambda x, y: round(100 * (cpu[y][1] - cpu[x][1]) / (cpu[y][0] - cpu[x][0]), 2)
result = {
    'scene': SCENE, 'convolutions': 2 * len(scene.speakers), 'callback_status': pos['status'],
    'xt_active_vs_offline': lag_of(mono(rec[:, 0:2]), ref_m, seg1),
    'xt_active_marker': lag_of(mono(rec[:, 14:16]), music_m, seg1),
    'bypass_main': lag_of(mono(rec[:, 0:2]), music_m, seg2),
    'bypass_marker': lag_of(mono(rec[:, 14:16]), music_m, seg2),
    'audiodg_cpu_percent_of_one_core': {'xt_active': pct('a0', 'a1'), 'bypass': pct('b0', 'b1')},
}
result['xt_added_samples'] = result['xt_active_vs_offline']['lag'] - result['xt_active_marker']['lag']
print(json.dumps(result))
json.dump(result, open(os.path.join(WORK, f'xt_scene_{SCENE}_{int(time.time())}.json'), 'w'), indent=1)
