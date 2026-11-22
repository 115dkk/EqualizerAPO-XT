"""Offline: build each scene's reference output, plugin feed, and timing with the installed Benchmark."""
import json, os, subprocess, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenes import Scene

WORK, ASSETS, PRODUCT = sys.argv[1], sys.argv[2], sys.argv[3]
names = sys.argv[4].split(',')
chunk_modes = sys.argv[5].split(',') if len(sys.argv) > 5 else ['keep']
RATE = 48000
music, _ = sf.read(os.path.join(WORK, 'music48.wav'), dtype='float32', always_2d=True)
x = np.zeros((music.shape[0], 16), dtype=np.float32)
x[:, 0:2] = music
src = os.path.join(WORK, 'in16-scene.wav')
sf.write(src, x, RATE, subtype='FLOAT')


def bench(folder, lines, out, batch='480'):
    open(os.path.join(folder, 'config.txt'), 'wb').write(('\r\n'.join(lines) + '\r\n').encode('utf-8'))
    run = subprocess.run([os.path.join(PRODUCT, 'Benchmark.exe'), '-i', src, '-o', out, '--batchsize', batch,
                          '--nopause', '--config-path', folder], capture_output=True, text=True, timeout=600)
    keep = [l.strip() for l in run.stdout.splitlines() if any(k in l for k in ('Real latency', 'real time', 'Realtime', 'x real', 'per block', 'Average', 'Max'))]
    return run.returncode, keep


for name in names:
    for mode in chunk_modes:
        sc = Scene(name, ASSETS, WORK)
        d = os.path.join(WORK, f'scene-{name}-{mode}')
        os.makedirs(d, exist_ok=True)
        sc.write_brir(d)
        keep = mode == 'keep'
        ref = os.path.join(WORK, f'ref-{name}-{mode}.wav')
        code, info = bench(d, ['Copy: L=1 R=2', 'Channel: L R'] + sc.chain_lines(keep) + ['Copy: 1=L 2=R'], ref)
        y, _ = sf.read(ref, dtype='float32', always_2d=True)
        feed = os.path.join(WORK, f'feed-{name}-{mode}.wav')
        n = len(sc.plugin_out)
        code2, _ = bench(d, ['Copy: L=1 R=2', 'Channel: L R'] + sc.plugin_lines(keep) +
                         ['Copy: ' + ' '.join(f'{i + 3}=V{s}' for i, s in enumerate(sc.plugin_out)), 'Copy: 1=0 2=0'], feed)
        f, _ = sf.read(feed, dtype='float32', always_2d=True)
        print(json.dumps({'scene': name, 'chunk': mode, 'exit': [code, code2], 'info': info,
                          'out_rms_ch1_2': [round(float(np.sqrt(np.mean(y[RATE:, c] ** 2))), 5) for c in (0, 1)],
                          'feed_rms': [round(float(np.sqrt(np.mean(f[RATE:, c] ** 2))), 4) for c in range(2, 2 + n)],
                          'convolutions': 2 * len(sc.speakers)}))
        for batch in ('480', '128'):
            code3, info3 = bench(d, ['Copy: L=1 R=2', 'Channel: L R'] + sc.chain_lines(keep) + ['Copy: 1=L 2=R'],
                                 os.path.join(WORK, 'scratch-out.wav'), batch)
            print(json.dumps({'scene': name, 'chunk': mode, 'batch': batch, 'info': info3}))
