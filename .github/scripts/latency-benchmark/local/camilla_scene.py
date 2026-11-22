"""CamillaDSP in real time through the virtual cable for one scene, measured against a raw
marker carried in the same stream. Does not touch the XT configuration.
mode 'feed' : the player sends the plugin's output (made offline by XT) on device ch 3.. and the
              music on ch 15/16; CamillaDSP convolves and plays to ch 1/2.
mode 'front': the player sends only the music on ch 15/16; a front-end program started by the
              caller upmixes it onto ch 3..; CamillaDSP convolves and plays to ch 1/2.
Added delay = lag(ch 1/2 vs XT's offline output) - lag(ch 15/16 vs the music)."""
import datetime, json, os, subprocess, sys, time
import numpy as np
import psutil
import sounddevice as sd
import soundfile as sf
from scipy.signal import fftconvolve
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenes import Scene

WORK, ASSETS, SCENE, MODE = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[5]
settings = json.loads(sys.argv[4])
REPEAT = int(sys.argv[6]) if len(sys.argv) > 6 else 1
import front
RATE = 48000
RENDER = 'CABLE In 16ch(VB-Audio Virtual Cable)'
CAPTURE = 'CABLE Output(VB-Audio Virtual Cable)'
SEG = 20 * RATE
LEAD = (22 if MODE == 'front' else 10) * RATE
FRONT_MAP = {'FL': 2, 'FR': 3, 'FC': 4, 'LFE': 5, 'SL': 6, 'SR': 7, 'TFL': 8, 'TFR': 9, 'TBL': 10, 'TBR': 11, 'BL': 5, 'BR': 5}
BASE = 2
cdsp = os.path.join(WORK, 'camilladsp', 'camilladsp.exe')
scene = Scene(SCENE, ASSETS, WORK)
brir_path = scene.write_brir(WORK).replace('\\', '/')
music, _ = sf.read(os.path.join(WORK, 'music48.wav'), dtype='float32', always_2d=True)
ref, _ = sf.read(os.path.join(WORK, f'ref-{SCENE}-keep.wav'), dtype='float32', always_2d=True)
feed, _ = sf.read(os.path.join(WORK, f'feed-{SCENE}-keep.wav'), dtype='float32', always_2d=True)
NPLUG = len(scene.plugin_out)
NCONV = 2 * len(scene.speakers)


def device(name, want_input):
    wasapi = [i for i, a in enumerate(sd.query_hostapis()) if a['name'] == 'Windows WASAPI'][0]
    hits = [i for i, d in enumerate(sd.query_devices()) if d['hostapi'] == wasapi and d['name'] == name
            and (d['max_input_channels'] if want_input else d['max_output_channels']) == 16]
    assert len(hits) == 1, 'device lookup failed'
    return hits[0]


def yaml_config(chunk, target, multithreaded):
    out = ['devices:', f'  samplerate: {RATE}', f'  chunksize: {chunk}', f'  target_level: {target}',
           '  enable_rate_adjust: false', f'  multithreaded: {"true" if multithreaded else "false"}',
           '  capture:', '    type: Wasapi', '    channels: 16', f'    device: "{CAPTURE}"', '    format: F32',
           '  playback:', '    type: Wasapi', '    channels: 16', f'    device: "{RENDER}"', '    format: F32',
           'mixers:', '  fan:', '    channels:', '      in: 16', f'      out: {NCONV}', '    mapping:']
    for n, speaker in enumerate(scene.speakers):
        for ear in (0, 1):
            out += [f'      - dest: {2 * n + ear}', '        sources:']
            for ch, gain in scene.sources(speaker):
                out += [f'          - channel: {FRONT_MAP[scene.plugin_out[ch]] if MODE == "front" else BASE + ch}', f'            gain: {20 * np.log10(gain):.4f}', '            inverted: false']
    out += ['  sum:', '    channels:', f'      in: {NCONV}', '      out: 16', '    mapping:']
    for ear in (0, 1):
        out += [f'      - dest: {ear}', '        sources:']
        for n in range(len(scene.speakers)):
            out += [f'          - channel: {2 * n + ear}', '            gain: 0', '            inverted: false']
    out += ['filters:']
    for n, speaker in enumerate(scene.speakers):
        for ear, prefix in ((0, 'V'), (1, 'E')):
            out += [f'  conv{2 * n + ear}:', '    type: Conv', '    parameters:', '      type: Wav',
                    f'      filename: "{brir_path}"', f'      channel: {scene.tracks[prefix + speaker]}']
    out += ['pipeline:', '  - type: Mixer', '    name: fan']
    for k in range(NCONV):
        out += ['  - type: Filter', f'    channels: [{k}]', '    names:', f'      - conv{k}']
    out += ['  - type: Mixer', '    name: sum']
    return '\n'.join(out) + '\n'


def lag_of(recorded, reference, seg_start):
    lo = max(0, seg_start - RATE // 2)
    piece = recorded[lo:seg_start + SEG + 2 * RATE]
    core = reference[4 * RATE:16 * RATE]
    corr = fftconvolve(piece, core[::-1], mode='valid')
    k = int(np.argmax(np.abs(corr)))
    lag = lo + k - (seg_start + 4 * RATE)
    got = piece[k:k + core.size]
    gain = float(np.dot(got, core) / np.dot(core, core))
    resid = got - gain * core
    resid_db = 10 * np.log10(np.sum(resid ** 2) / max(np.sum((gain * core) ** 2), 1e-30) + 1e-30)
    win = RATE // 100
    blocks = resid[:resid.size // win * win].reshape(-1, win)
    refb = (gain * core)[:blocks.size].reshape(-1, win)
    ratio = np.sum(blocks ** 2, axis=1) / (np.sum(refb ** 2, axis=1) + 1e-12)
    spikes = int(np.sum((ratio > 0.01) & (ratio > 10 * np.median(ratio))))
    return int(lag), round(gain, 4), round(float(resid_db), 2), spikes


mono = lambda x: x.mean(axis=1).astype(np.float64)
total = LEAD + SEG + 3 * RATE
out = np.zeros((total, 16), dtype=np.float32)
if MODE == 'feed':
    out[LEAD:LEAD + SEG, BASE:BASE + NPLUG] = feed[:SEG, 2:2 + NPLUG]
out[LEAD:LEAD + SEG, 14:16] = music[:SEG]
in_dev, out_dev = device(CAPTURE, True), device(RENDER, False)
BAD = ('Playback interrupted', 'buffer underrun', 'samples were dropped', 'dropping chunk', 'pausing stream', 'above 100%')
rows = []
for chunk, target, multi in [x for x in settings for _ in range(REPEAT)]:
    tag = f'{SCENE}-{MODE}-c{chunk}-t{target}-m{int(multi)}'
    yml = os.path.join(WORK, f'cdsp-{tag}.yml')
    log = os.path.join(WORK, f'cdsp-{tag}.log')
    open(yml, 'w', encoding='utf-8').write(yaml_config(chunk, target, multi))
    if os.path.exists(log):
        os.remove(log)
    check = subprocess.run([cdsp, '-c', yml], capture_output=True, text=True, timeout=60)
    if check.returncode != 0:
        rows.append({'setting': tag, 'error': 'config rejected', 'detail': (check.stdout + check.stderr)[-300:]})
        print(json.dumps(rows[-1]))
        continue
    proc = None
    fr = None
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

    marks = {}
    try:
        with sd.Stream(samplerate=RATE, blocksize=0, latency='high', device=(in_dev, out_dev), channels=(16, 16),
                       dtype='float32', callback=callback) as stream:
            if MODE == 'front':
                front.write_settings(0, 480)
                fr = front.Front()
                time.sleep(9)
            proc = subprocess.Popen([cdsp, '-o', log, yml], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            watch = [psutil.Process(proc.pid)] + ([psutil.Process(fr.pid)] if fr else [])
            read = lambda: [sum(p.cpu_times()[:2]) for p in watch]
            while stream.active:
                if 'a' not in marks and pos['o'] >= LEAD and proc.poll() is None:
                    marks['a'] = (time.time(), read())
                if 'a' in marks and 'b' not in marks and pos['o'] >= LEAD + SEG and proc.poll() is None:
                    marks['b'] = (time.time(), read())
                time.sleep(0.05)
        alive = proc.poll() is None
    finally:
        if fr is not None:
            fr.close()
            front.write_settings(1, 480)
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
    cpu = None
    if 'a' in marks and 'b' in marks:
        wall = marks['b'][0] - marks['a'][0]
        cpu = [round(100 * (y - x) / wall, 1) for x, y in zip(marks['a'][1], marks['b'][1])]
    counts = {}
    if os.path.exists(log) and 'a' in marks and 'b' in marks:
        seg_from = datetime.datetime.fromtimestamp(marks['a'][0])
        seg_to = datetime.datetime.fromtimestamp(marks['b'][0])
        for line in open(log, encoding='utf-8', errors='replace'):
            try:
                stamp = datetime.datetime.strptime(line[:26], '%Y-%m-%d %H:%M:%S.%f')
            except ValueError:
                continue
            if seg_from <= stamp <= seg_to:
                for b in BAD:
                    if b in line:
                        counts[b] = counts.get(b, 0) + 1
    marker = lag_of(mono(rec[:, 14:16]), mono(music[:SEG]), LEAD)
    main = lag_of(mono(rec[:, 0:2]), mono(ref[:SEG, 0:2]), LEAD)
    row = {'setting': tag, 'convolutions': NCONV, 'alive_at_end': alive, 'marker_lag': marker[0],
           'main_lag': main[0], 'main_gain': main[1], 'main_residual_db': main[2], 'main_residual_spikes': main[3],
           'added_samples': main[0] - marker[0], 'added_ms': round((main[0] - marker[0]) / RATE * 1000, 2),
           'log_problem_lines': counts, 'callback_status': pos['status'], 'cpu_percent_of_one_core': cpu,
           'rms_by_channel': [round(float(np.sqrt(np.mean(rec[LEAD + RATE:LEAD + SEG, c] ** 2))), 4) for c in range(16)]}
    rows.append(row)
    print(json.dumps(row))
    time.sleep(1.5)
json.dump(rows, open(os.path.join(WORK, f'camilla_scene_{SCENE}_{MODE}_{int(time.time())}.json'), 'w'), indent=1)
os.remove(brir_path)
