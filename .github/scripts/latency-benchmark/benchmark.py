"""Runner-only measurements. Never serialize input text, audio, or exceptions.

Every child log stays in Work. Summary is constructed from numeric observations
and fixed diagnostic messages, never by copying a program's complete output.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import winreg
import zipfile

import numpy as np
import scipy.signal as signal
import soundfile as sf
import yaml

RATE = 48000
VIRTUAL = ['VFL', 'VFR', 'VFC', 'VLFE', 'VBL', 'VBR', 'VSL', 'VSR',
           'VTFL', 'VTFR', 'VTBL', 'VTBR']
XT_TAG = 'v2.77.0'
XT_ASSET = 'EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe'
XT_HASH = '172f87a8d84be3ba06e9d8bb10483a44fcb9a48ca322b8152272555bd7ad7a4b'
CAMILLA_HASH = 'd949de5ac5fc13282dd47155e0ac67087c09dd84baf7c7d568616bfc6e43d190'
NUMBER = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'


class MeasurementError(Exception):
    """Messages are fixed by the harness, never input-derived."""


def require(condition, message):
    if not condition:
        raise MeasurementError(message)


def cpu_seconds(process):
    times = [wintypes.FILETIME() for _ in range(4)]
    api = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    api.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    api.restype = wintypes.BOOL
    require(api(int(process._handle), *(ctypes.byref(t) for t in times)),
            'GetProcessTimes failed.')
    return sum((t.dwHighDateTime << 32) + t.dwLowDateTime for t in times[2:]) / 1e7


class Bench:
    def __init__(self, args):
        self.args = args
        self.work = Path(args.work)
        self.inputs = Path(args.inputs)
        self.summary = Path(args.summary)
        self.data = {
            'machine': json.loads((self.work / 'machine.json').read_text(encoding='utf-8-sig')),
            'versions': {'XT': XT_TAG, 'CamillaDSP': '4.1.3'},
            'downloads': {}, 'phase_a': [], 'comparisons': [], 'phase_b': [],
            'steps': {s: 'not measured: preceding phase not completed'
                      for s in ['A1', 'A2', 'A3', 'A4', 'A5', 'B0', 'B1', 'B2', 'B3', 'B4', 'B5']},
            'notes': [
                'XT CPU percent is Benchmark elapsed processing time divided by audio duration; it is not aggregate worker CPU time.',
                'Process CPU seconds include user plus kernel time; wall time includes startup and file I/O.',
                'Benchmark writes PCM16 WAV and ignores -c and -r when -i is supplied. The feed therefore uses a padded 12-channel input WAV.',
                'Positive comparison lag means the tested signal is later than the reference.',
                'Offline comparisons use one scalar gain for both ears and a 30-second excerpt starting at 10 seconds.',
                'Real latency is the engine-reported value, not a measured transport delay.',
            ],
        }
        self.save()

    def save(self):
        (self.summary / 'results.json').write_text(json.dumps(self.data, indent=2, allow_nan=False), encoding='ascii')
        text = ['# Latency benchmark', '', f"Run {self.data['machine']['run_id']}", '',
                '## Phase A throughput', '',
                '| Program | Batch/chunk | Threads | Repeat | Wall s | CPU s | x realtime | XT CPU % | Reported latency samples |',
                '|---|---:|---|---:|---:|---:|---:|---:|---:|']
        def val(value):
            return f'{value:.6g}' if isinstance(value, float) else str(value) if value is not None else '-'
        for row in self.data['phase_a']:
            text.append('| ' + ' | '.join(val(row.get(k)) for k in
                ['program', 'frames', 'threads', 'repeat', 'wall_s', 'cpu_s', 'realtime_factor', 'xt_cpu_percent', 'latency_samples']) + ' |')
        text += ['', '## Correctness', '', '| Comparison | Lag samples | Lag ms | Gain | Residual dB | Verdict |',
                 '|---|---:|---:|---:|---:|---|']
        for row in self.data['comparisons']:
            text.append('| ' + ' | '.join(val(row.get(k)) for k in
                ['name', 'lag_samples', 'lag_ms', 'gain', 'residual_display', 'verdict']) + ' |')
        text += ['', '## Phase B latency', '', '| Program | Setting | Added delay samples | ms | Dropouts | CPU s |',
                 '|---|---|---:|---:|---|---:|']
        for row in self.data['phase_b']:
            text.append('| ' + ' | '.join(val(row.get(k)) for k in
                ['program', 'setting', 'added_samples', 'added_ms', 'dropouts', 'cpu_s']) + ' |')
        text += ['', '## Step status', ''] + [f'- {key}: {value}' for key, value in self.data['steps'].items()]
        text += ['', '## Measurement definitions and limitations', ''] + ['- ' + x for x in self.data['notes']]
        (self.summary / 'results.md').write_text('\n'.join(text) + '\n', encoding='ascii')

    def run(self, label, command, timeout=300, cwd=None, allowed=(0,)):
        # stdout/stderr files prevent pipe deadlocks and keep arbitrary vendor
        # output, including any echoed state, out of the public runner log.
        start = time.perf_counter()
        with (self.work / (label + '.stdout.log')).open('wb') as out, (self.work / (label + '.stderr.log')).open('wb') as err:
            p = subprocess.Popen([str(x) for x in command], cwd=cwd or self.args.workspace,
                                 stdout=out, stderr=err)
            try:
                p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
                p.wait(timeout=10)
                raise MeasurementError(label + ' exceeded its process timeout.') from None
            wall = time.perf_counter() - start
            cpu = cpu_seconds(p)
        require(p.returncode in allowed, label + ' failed (exit ' + str(p.returncode) + ').')
        return {'wall_s': wall, 'cpu_s': cpu, 'exit_code': p.returncode}

    def release(self, repo, tag, asset, expected, label):
        directory = self.work / ('download-' + label)
        directory.mkdir()
        self.run('download-' + label, ['gh', 'release', 'download', tag, '--repo', repo,
                                      '--pattern', asset, '--dir', directory], 300)
        path = directory / asset
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        self.data['downloads'][label] = {'sha256': digest, 'asset': asset, 'version': tag}
        self.save()
        require(digest == expected, label + ' download SHA-256 differs from the pin.')
        return path

    def install(self):
        setup = self.release('115dkk/EqualizerAPO-XT', XT_TAG, XT_ASSET, XT_HASH, 'XT')
        self.run('install-XT', [setup, '--silent'], 600)
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\EqualizerAPO') as key:
            self.current = Path(winreg.QueryValueEx(key, 'InstallPath')[0])
            self.config_dir = Path(winreg.QueryValueEx(key, 'ConfigPath')[0])
        if not (self.current / 'Benchmark.exe').exists():
            self.current /= 'current'
        self.xt = self.current / 'Benchmark.exe'
        require(self.xt.exists(), 'Installed XT has no Benchmark.exe.')
        self.run('install-upmixer', [self.inputs / 'zAuro-0.4.1-Windows-x64-Setup.exe',
                 '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-'], 300)
        archive = self.release('HEnquist/camilladsp', 'v4.1.3', 'camilladsp-windows-amd64.zip', CAMILLA_HASH, 'CamillaDSP')
        extracted = self.work / 'camilla'
        extracted.mkdir()
        with zipfile.ZipFile(archive) as z:
            z.extractall(extracted)
        self.camilla = next(extracted.rglob('camilladsp.exe'))

    def prepare(self):
        audio, rate = sf.read(self.inputs / 'bp_MUS_CosmosBATTLE.ogg', frames=90 * 192000, always_2d=True, dtype='float64')
        require(audio.shape[1] == 2, 'Music input is not stereo.')
        audio = audio[:90 * rate]
        if rate != RATE:
            divisor = math.gcd(rate, RATE)
            audio = signal.resample_poly(audio, RATE // divisor, rate // divisor, axis=0)
        self.duration = len(audio) / RATE
        require(self.duration >= 40, 'Input has fewer than 40 seconds of audio.')
        self.music = self.work / 'music.wav'
        sf.write(self.music, audio, RATE, subtype='FLOAT')
        padded = np.zeros((len(audio), 12), dtype=np.float32)
        padded[:, :2] = audio
        self.padded = self.work / 'music-12.wav'
        sf.write(self.padded, padded, RATE, subtype='FLOAT')
        config = (self.inputs / 'config-714.txt').read_text(encoding='utf-8-sig')
        # This text is intentionally never printed or added to any summary.
        self.lines = [x.split('#', 1)[0].strip() for x in config.splitlines()]
        self.lines = [x for x in self.lines if x]
        positions = [i for i, x in enumerate(self.lines) if x.startswith('VSTPlugin:')]
        require(len(positions) == 1, 'Expected exactly one VSTPlugin command.')
        self.plugin_index = positions[0]
        slots = re.search(r'\bOutputChannels\s+([^\s]+)', self.lines[self.plugin_index])
        require(slots is not None, 'Plugin needs an explicit output slot fill for an unambiguous twelve-channel export.')
        self.virtual = slots[1].strip('"').split(',')
        require(len(self.virtual) == 12 and len(set(self.virtual)) == 12, 'Plugin output fill is not twelve distinct channels.')
        self.data['feed_slot_names_match_brief'] = [a == b for a, b in zip(self.virtual, VIRTUAL)]
        convs = [x for x in self.lines if x.startswith('MultiConvolution:')]
        require(len(convs) == 1, 'Expected exactly one MultiConvolution command.')
        match = re.search(r'("[^"\r\n]+\.wav"|[^\s"]+\.wav)\s*$', convs[0], re.I)
        require(match is not None, 'Cannot resolve the relative impulse response filename.')
        filename = match[1].strip('"')
        require(Path(filename).name == filename and ':' not in filename, 'Impulse filename is not a simple relative path.')
        self.full_config = self.work / 'full-config'
        self.full_config.mkdir()
        (self.full_config / 'config.txt').write_text(config, encoding='utf-8')
        shutil.copyfile(self.inputs / 'brir22.wav', self.full_config / filename)
        self.brir = self.inputs / 'brir22.wav'
        info = sf.info(self.brir)
        require(info.channels == 22 and info.samplerate == RATE, 'BRIR must have 22 channels at 48000 Hz.')
        self.data['audio'] = {'sample_rate': RATE, 'seconds': self.duration, 'brir_channels': info.channels,
                              'brir_frames': info.frames, 'feed_channel_order': VIRTUAL}
        self.feed_config = self.work / 'feed-config'
        self.feed_config.mkdir()
        # Benchmark passes a zero channel mask. XT names a 12-channel
        # device numerically, unlike stereo, so preserve the original L/R
        # names as aliases for the first two padded input channels.
        prefix = 'Copy: L=1 R=2\nChannel: L R\n' + '\n'.join(self.lines[:self.plugin_index + 1])
        copy = 'Copy: ' + ' '.join(f'{i + 1}={name}' for i, name in enumerate(self.virtual))
        (self.feed_config / 'config.txt').write_text(prefix + '\n' + copy + '\n', encoding='utf-8')
        self.save()

    def xt_run(self, label, frames, config, input_wav, output_wav, fixed=None):
        command = [self.xt, '-i', input_wav, '-o', output_wav, '-c', '12' if config == self.feed_config else '2',
                   '-r', str(RATE), '--batchsize', str(frames), '--config-path', config, '--nopause', '-v']
        if fixed is not None:
            command += ['--fanout-fixed', str(fixed)]
        result = self.run(label, command, 600, self.current)
        stdout = (self.work / (label + '.stdout.log')).read_text(errors='replace')
        approved = []
        for line in stdout.splitlines():
            if re.fullmatch(r'(?:Real latency: \d+ samples \([\d.]+ ms\)|Real latency by output channel:(?: \d+)+ samples|Convolution fan-out: team of \d+ workers, (?:fixed count|the last block woke \d+)|This is equivalent to [\d.eE+-]+% CPU load \(one core\) when processing in real time|Batch timings \(n=\d+, \d+ frames each, microseconds\):|\s+(?:min|median|mean|p95|p99|max):\s+[\d.eE+-]+)', line):
                approved.append(line)
        (self.summary / (label + '.log')).write_text('\n'.join(approved) + '\n', encoding='ascii')
        latency = re.search(r'Real latency: (\d+) samples', stdout)
        cpu = re.search(r'This is equivalent to (' + NUMBER + r')% CPU load', stdout)
        require(latency and cpu, 'XT did not report latency and processing cost.')
        result.update({'latency_samples': int(latency[1]), 'xt_cpu_percent': float(cpu[1]),
                       'batch_us': {name: float(value) for name, value in re.findall(r'^\s+(min|median|mean|p95|p99|max):\s+(' + NUMBER + ')', stdout, re.M)}})
        require(len(result['batch_us']) == 6, 'XT did not report all batch timing percentiles.')
        fanout = re.search(r'Convolution fan-out: team of (\d+) workers(?:, the last block woke (\d+))?', stdout)
        if fanout:
            result['workers'] = int(fanout[1])
            result['last_workers_woken'] = int(fanout[2]) if fanout[2] else None
        wav, rate = sf.read(output_wav, always_2d=True)
        require(rate == RATE and np.isfinite(wav).all(), 'XT produced invalid audio.')
        result.update({'output_peak': float(np.max(np.abs(wav))),
                       'pcm16_rail_fraction': float(np.mean(np.abs(wav) >= 32767 / 32768)),
                       'audio_s': len(wav) / RATE, 'realtime_factor': len(wav) / RATE / result['wall_s']})
        return result

    def phase_a1(self):
        for frames in [480, 128]:
            for rep in range(1, 4):
                row = self.xt_run(f'XT-{frames}-{rep}', frames, self.full_config, self.music,
                                  self.work / f'XT-{frames}-{rep}.wav')
                row.update({'program': 'XT', 'frames': frames, 'threads': 'adaptive', 'repeat': rep})
                self.data['phase_a'].append(row)
                self.save()
        row = self.xt_run('XT-single', 480, self.full_config, self.music, self.work / 'XT-single.wav', 0)
        row.update({'program': 'XT', 'frames': 480, 'threads': 'single', 'repeat': 1})
        self.data['phase_a'].append(row)
        self.data['steps']['A1'] = 'measured: three repetitions at 480 and 128; one fixed-zero-worker run at 480'
        self.save()

    def phase_a2(self):
        self.feed = self.work / 'feed-12.wav'
        row = self.xt_run('XT-feed', 480, self.feed_config, self.padded, self.feed)
        audio, _ = sf.read(self.feed, always_2d=True)
        rms = np.sqrt(np.mean(audio * audio, axis=0))
        row['rms_per_channel'] = rms.tolist()
        self.data['feed'] = row
        self.save()
        require(len(rms) == 12 and np.any(rms > 1e-7), 'Upmixed feed is silent or has the wrong channel count.')
        # A silent slot can be intentional plugin state (notably LFE). Export
        # each pair with the ORIGINAL stereo device and compare numerically,
        # so a name/layout mistake cannot masquerade as an intentional zero.
        oracle = np.zeros_like(audio)
        prefix = '\n'.join(self.lines[:self.plugin_index + 1])
        for pair in range(6):
            directory = self.work / f'feed-oracle-{pair}'
            directory.mkdir()
            copy = f'Copy: L={self.virtual[2 * pair]} R={self.virtual[2 * pair + 1]}'
            (directory / 'config.txt').write_text(prefix + '\n' + copy + '\n', encoding='utf-8')
            output = self.work / f'feed-oracle-{pair}.wav'
            self.xt_run(f'feed-oracle-{pair}', 480, directory, self.music, output)
            samples, _ = sf.read(output, always_2d=True)
            oracle[:, pair * 2:pair * 2 + 2] = samples
        row['stereo_device_oracle_rms_error'] = np.sqrt(np.mean((audio - oracle) ** 2, axis=0)).tolist()
        row['silent_slots_zero_based'] = np.flatnonzero(rms <= 1e-7).tolist()
        self.save()
        require(np.max(np.abs(audio - oracle)) <= 1 / 32768, 'Twelve-channel feed differs from original stereo-device plugin output.')
        require(row['pcm16_rail_fraction'] == 0, 'PCM16 upmixed feed hits the rails; convolution comparison would be invalid.')
        self.data['steps']['A2'] = 'measured: all 12 slots verified against six stereo-device exports; any silent slots are also silent in the original plugin context; PCM16 quantization applies'
        self.save()

    @staticmethod
    def assignments(text):
        # Restrict rather than guess unsupported grammar. Error text is fixed.
        compact = re.sub(r'\s*([=+*])\s*', r'\1', text)
        pairs = [term.split('=', 1) for term in compact.split()]
        require(all(len(pair) == 2 for pair in pairs), 'Unsupported assignment grammar.')
        return pairs

    @staticmethod
    def expression(expression, env):
        out = np.zeros(len(next(iter(env.values()))))
        for term in re.split(r'(?<![eE])\+', expression):
            if '*' in term:
                factor, source = term.split('*', 1)
                gain = 10 ** (float(factor[:-2]) / 20) if factor.lower().endswith('db') else float(factor)
            else:
                gain, source = 1.0, term
            if source in ('0', '0.0'):
                continue
            require(source in env, 'Copy expression references an unsupported channel or nonzero constant.')
            out += gain * env[source]
        return out

    def translate(self):
        env = {name: np.eye(12)[i] for i, name in enumerate(self.virtual)}
        # L/R still carry the plugin inputs unless overwritten. Reject their
        # use rather than equating them with virtual front-left/front-right.
        conv = None
        for line in self.lines[self.plugin_index + 1:]:
            command, args = line.split(':', 1)
            if command == 'Copy':
                changes = {name: self.expression(expression, env) for name, expression in self.assignments(args)}
                env.update(changes)
            elif command == 'MultiConvolution':
                require(conv is None, 'Multiple convolution stages are unsupported.')
                match = re.search(r'("[^"\r\n]+\.wav"|[^\s"]+\.wav)\s*$', args, re.I)
                maps = self.assignments(args[:match.start()])
                require(len(maps) == 22, 'Expected 22 independently convolved channels.')
                conv = []
                for name, index in maps:
                    require(name in env and re.fullmatch(r'\d+', index), 'Unsupported convolution channel or coefficient expression.')
                    require(0 <= int(index) < 22, 'BRIR index outside its 22 channels.')
                    conv.append({'index': int(index), 'feed': env[name].tolist(), 'name': name})
                # Existing channels not convolved cannot be represented by the
                # new basis. The target config is expected to sum just these.
                env = {item['name']: np.eye(22)[i] for i, item in enumerate(conv)}
            elif command == 'Channel':
                continue
            else:
                raise MeasurementError('Unsupported post-plugin command; translation stopped rather than approximated.')
        require(conv is not None and 'L' in env and 'R' in env, 'Missing convolution or final stereo sum.')
        self.conv = conv
        self.final_matrix = [env['L'].tolist(), env['R'].tolist()]
        self.data['mapping'] = {'brir_indices': [x['index'] for x in conv],
                                'input_matrix': [x['feed'] for x in conv], 'output_matrix': self.final_matrix}
        self.save()

    @staticmethod
    def mixer(matrix):
        return {'channels': {'in': len(matrix[0]), 'out': len(matrix)},
                'mapping': [{'dest': i, 'sources': [{'channel': j, 'gain': float(g), 'scale': 'linear'}
                             for j, g in enumerate(row) if g != 0]} for i, row in enumerate(matrix)]}

    def camilla_config(self, frames, threaded, output):
        filters = {f'brir{i}': {'type': 'Conv', 'parameters': {'type': 'Wav', 'filename': str(self.brir), 'channel': item['index']}}
                   for i, item in enumerate(self.conv)}
        return {'devices': {'samplerate': RATE, 'chunksize': frames, 'multithreaded': threaded,
                            'capture': {'type': 'WavFile', 'filename': str(self.feed)},
                            'playback': {'type': 'File', 'channels': 2, 'format': 'F64_LE', 'filename': str(output)}},
                'mixers': {'fanout': self.mixer([x['feed'] for x in self.conv]), 'sum': self.mixer(self.final_matrix)},
                'filters': filters,
                'pipeline': [{'type': 'Mixer', 'name': 'fanout'}] +
                            [{'type': 'Filter', 'channels': [i], 'names': [f'brir{i}']} for i in range(22)] +
                            [{'type': 'Mixer', 'name': 'sum'}]}

    def phase_a3(self):
        self.translate()
        for frames in [1024, 512, 128]:
            for threaded in [False, True]:
                label = f'Camilla-{frames}-{int(threaded)}'
                output = self.work / (label + '.raw')
                config = self.work / (label + '.yml')
                config.write_text(yaml.safe_dump(self.camilla_config(frames, threaded, output)), encoding='utf-8')
                self.run(label + '-check', [self.camilla, '-c', config], 60)
                row = self.run(label, [self.camilla, '-o', self.work / (label + '.program.log'), config], 600)
                row.update({'program': 'CamillaDSP', 'frames': frames, 'threads': 'multi' if threaded else 'single',
                            'repeat': 1, 'realtime_factor': self.duration / row['wall_s'],
                            'processing_load': None})
                audio = np.fromfile(output, dtype='<f8')
                require(len(audio) > RATE * 2 * 40 and len(audio) % 2 == 0 and np.isfinite(audio).all(), 'CamillaDSP output is missing, short, or invalid.')
                sf.write(self.work / (label + '.wav'), audio.reshape(-1, 2), RATE, subtype='FLOAT')
                self.data['phase_a'].append(row)
                self.save()
        self.data['steps']['A3'] = 'measured: all six file-throughput settings; no realtime processing-load polling'
        self.save()

    def compare(self, name, tested, reference, zero_lag=False):
        y, rate_y = sf.read(tested, always_2d=True, dtype='float64')
        x, rate_x = sf.read(reference, always_2d=True, dtype='float64')
        require(rate_y == rate_x == RATE and y.shape[1] == x.shape[1] == 2, 'Comparison format mismatch.')
        start, count = 10 * RATE, 30 * RATE
        require(min(len(x), len(y)) >= start + count + RATE, 'Comparison audio is too short.')
        a, b = y[start:start + count, 0], x[start:start + count, 0]
        correlation = signal.correlate(a - a.mean(), b - b.mean(), method='fft')
        lags = signal.correlation_lags(len(a), len(b))
        allowed = np.abs(lags) <= RATE * 2
        lag = int(lags[allowed][np.argmax(np.abs(correlation[allowed]))])
        x = x[start:start + count]
        y = y[start + lag:start + lag + count]
        require(float(np.sum(y * y)) > 0 and float(np.sum(x * x)) > 0, 'Comparison signal is silent.')
        gain = float(np.sum(x * y) / np.sum(y * y))
        residual = x - gain * y
        ratio = float(np.sum(residual * residual) / np.sum(x * x))
        db = float(10 * np.log10(ratio)) if ratio > 0 else None
        row = {'name': name, 'lag_samples': lag, 'lag_ms': lag * 1000 / RATE, 'gain': gain,
               'residual_db': db, 'residual_display': db if db is not None else 'exact PCM16 match',
               'residual_power_ratio': ratio, 'excerpt_start_s': 10, 'excerpt_s': 30,
               'verdict': 'pass' if ratio < 10 ** (-45 / 10) and (not zero_lag or lag == 0) else 'fail'}
        self.data['comparisons'].append(row)
        self.save()
        return row['verdict'] == 'pass'

    def phase_a4(self):
        passed = self.compare('XT 128 vs 480', self.work / 'XT-128-1.wav', self.work / 'XT-480-1.wav', True)
        for frames in [1024, 512, 128]:
            for threaded in [0, 1]:
                ok = self.compare(f'Camilla {frames}/{threaded} vs XT', self.work / f'Camilla-{frames}-{threaded}.wav', self.work / 'XT-480-1.wav')
                passed = ok and passed
        require(passed, 'Correctness comparison failed the -45 dB residual or zero XT block-lag criterion; later phases stopped.')
        self.data['steps']['A4'] = 'measured: all comparisons pass; PCM16 quantization limits the residual'
        self.save()

    def phase_a5(self):
        setup = self.release('mgth/Omniphony', 'v0.7.0', 'omniphony-v0.7.0-windows-x86_64-setup.exe',
                             '7f184a5d87bbe4b6578b7de33fb34ed989a7cf342378cd1d27983939a89022f0', 'Omniphony')
        self.run('install-Omniphony', [setup, '/S'], 300)
        self.orender = Path(os.environ['LOCALAPPDATA']) / 'Programs/Omniphony/studio/orender.exe'
        require(self.orender.exists(), 'Omniphony installer did not provide orender.exe.')
        self.run('Omniphony-help', [self.orender, 'render', '--help'], 30)
        help_text = (self.work / 'Omniphony-help.stdout.log').read_text(errors='replace')
        self.data['omniphony'] = {'file_backend_in_installed_help': '--output-file' in help_text}
        source = self.work / 'omniphony-source'
        self.run('Omniphony-source', ['git', 'clone', '--depth', '1', '--branch', 'v0.7.0',
                                     'https://github.com/mgth/Omniphony.git', source], 300)
        self.run('Omniphony-source-revision', ['git', '-C', source, 'rev-parse', 'HEAD'], 30)
        revision = (self.work / 'Omniphony-source-revision.stdout.log').read_text().strip()
        require(revision == '353e135fa01e181bea36d83099a5951fd5c34180', 'Omniphony tag differs from the pinned commit.')
        archive = self.work / 'omniphony-source.tar'
        self.run('Omniphony-source-archive', ['git', '-C', source, 'archive', '--format=tar', '--output', archive, 'HEAD'], 90)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.data['downloads']['Omniphony-source'] = {'commit': revision, 'sha256': digest, 'format': 'git archive --format=tar HEAD'}
        self.save()
        expected = ''
        if not expected:
            self.data['steps']['A5'] = 'not measured: source SHA-256 discovery; source was not built before pinning'
            self.save()
            return
        require(digest == expected, 'Omniphony source SHA-256 differs from its pin.')
        # The upstream workspace resolves an unrelated git-patched SOFA reader.
        # Build the two bridge crates unchanged in a smaller workspace, retaining
        # the exact reachable packages and checksums from the upstream lock.
        import tomllib
        isolated = self.work / 'omni-bridge-build'
        isolated.mkdir()
        for crate in ['reference_bridge', 'bridge_api']:
            shutil.copytree(source / 'omniphony-renderer' / crate, isolated / crate)
        manifest = '''[workspace]
members = ["reference_bridge", "bridge_api"]
resolver = "2"
[workspace.package]
version = "0.7.0"
edition = "2024"
rust-version = "1.89"
license = "GPL-3.0-or-later"
[workspace.dependencies]
bridge_api = { version = "0.6.0", path = "bridge_api" }
abi_stable = "0.11"
log = "0.4.27"
'''
        (isolated / 'Cargo.toml').write_text(manifest, encoding='ascii')
        locktext = (source / 'omniphony-renderer/Cargo.lock').read_text()
        chunks = locktext.split('[[package]]')[1:]
        packages = [tomllib.loads('[[package]]' + chunk)['package'][0] for chunk in chunks]
        wanted = set()
        pending = [('reference_bridge', '0.7.0'), ('bridge_api', '0.6.0')]
        while pending:
            name, version = pending.pop()
            candidates = [i for i, package in enumerate(packages) if package['name'] == name and (version is None or package['version'] == version)]
            require(len(candidates) == 1, 'Cannot isolate an unambiguous dependency from the upstream Cargo lock.')
            index = candidates[0]
            if index in wanted:
                continue
            wanted.add(index)
            package = packages[index]
            require(not package.get('source', '').startswith('git+'), 'Reference bridge unexpectedly requires a non-allowlisted git dependency.')
            for dependency in package.get('dependencies', []):
                fields = dependency.split()
                pending.append((fields[0], fields[1] if len(fields) > 1 else None))
        (isolated / 'Cargo.lock').write_text('version = 4\n\n' + ''.join('[[package]]' + chunks[i] for i in sorted(wanted)), encoding='utf-8')
        self.data['omniphony']['bridge_build'] = self.run('Omniphony-bridge-build',
            ['pwsh', '-NoProfile', '-File', Path(__file__).with_name('Prepare-Omniphony.ps1'), '-Source', isolated, '-Work', self.work], 1500)
        bridge = self.work / 'omni-target/release/reference_bridge.dll'
        require(bridge.exists(), 'Reference bridge build did not produce its DLL.')
        self.data['omniphony']['check_bridges'] = self.run('Omniphony-check-bridges',
            [self.orender, 'check-bridges', '--bridge-path', bridge], 60)
        self.data['steps']['A5'] = 'bridge built and check-bridges passed; SOFA loading not yet measured'
        self.save()

    def execute(self):
        # Installation failures are harness failures; later failures preserve
        # completed measurements and return zero with explicit step status.
        try:
            self.install()
        except Exception as exc:
            self.data['harness'] = str(exc) if isinstance(exc, MeasurementError) else 'Product installation failed; logs retained only in Work.'
            self.save()
            return 1
        for name, action in [('prepare', self.prepare), ('A1', self.phase_a1), ('A2', self.phase_a2), ('A3', self.phase_a3), ('A4', self.phase_a4), ('A5', self.phase_a5)]:
            try:
                action()
            except Exception as exc:
                reason = str(exc) if isinstance(exc, MeasurementError) else 'Harness could not interpret this phase; exception type ' + type(exc).__name__
                self.data['steps'][name] = 'not completed: ' + reason
                # Trace locations, not exception text: parser errors can quote
                # a private config line or vendor-supplied state.
                import traceback
                self.data['diagnostic_locations'] = [{'function': frame.name, 'line': frame.lineno}
                                                    for frame in traceback.extract_tb(exc.__traceback__)
                                                    if Path(frame.filename).name == 'benchmark.py']
                self.save()
                return 0
        self.save()
        return 0


def main():
    require(os.environ.get('GITHUB_ACTIONS') == 'true' and os.environ.get('RUNNER_OS') == 'Windows',
            'Runner-only harness; local execution is prohibited.')
    parser = argparse.ArgumentParser()
    for name in ['workspace', 'inputs', 'work', 'summary']:
        parser.add_argument('--' + name, required=True)
    return Bench(parser.parse_args()).execute()


if __name__ == '__main__':
    raise SystemExit(main())
