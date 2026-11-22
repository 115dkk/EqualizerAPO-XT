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
import sys
import time
import winreg
import zipfile

import numpy as np
import scipy.signal as signal
import soundfile as sf
import yaml

# -I removes the script directory; add only our trusted checked-in helpers.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from camilla_load import LoadPoller

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
            'versions': {'XT': XT_TAG, 'CamillaDSP': '4.1.3', 'Omniphony': '0.7.0'},
            'python_wheels': json.loads((self.summary / 'wheel-hashes.json').read_text(encoding='utf-8-sig')),
            'downloads': {}, 'phase_a': [], 'comparisons': [], 'phase_b': [],
            'steps': {s: ('not measured: preceding phase not completed' if s.startswith('A') else
                          'not measured: preceding realtime phase not completed or later phase not implemented')
                      for s in ['A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'B0', 'B1', 'B2', 'B3', 'B4', 'B5']},
            'notes': [
                'XT CPU percent is Benchmark elapsed processing time divided by audio duration; it is not aggregate worker CPU time.',
                'Process CPU seconds include user plus kernel time; wall time includes startup and file I/O.',
                'Benchmark writes PCM16 WAV and ignores -c and -r when -i is supplied. The feed therefore uses a padded 12-channel input WAV.',
                'Positive comparison lag means the tested signal is later than the reference.',
                'Offline comparisons use one scalar gain for both ears and a 30-second excerpt starting at 10 seconds.',
                'Real latency is the engine-reported value, not a measured transport delay.',
                'XT throughput includes the upmixer plus convolution; CamillaDSP consumes the already-upmixed feed. These are not like-for-like whole-chain CPU costs.',
                'A future B2 comparison against the offline XT output subtracts baseline cable delay only: the reference already contains the VST delay, so subtracting reported VST latency again would be incorrect.',
            ],
        }
        self.save()

    def save(self):
        (self.summary / 'results.json').write_text(json.dumps(self.data, indent=2, allow_nan=False), encoding='ascii')
        cpu_names = ', '.join(cpu['Name'].strip() for cpu in self.data['machine']['cpu'])
        text = ['# Latency benchmark', '', f"Run {self.data['machine']['run_id']}", '',
                f'Runner CPU: {cpu_names}; 2 cores / 4 logical processors.', '',
                '## Phase A throughput', '',
                '| Program | Batch/chunk | Threads | Repeat | Wall s | CPU s | x realtime | XT CPU % | Reported latency samples |',
                '|---|---:|---|---:|---:|---:|---:|---:|---:|']
        def val(value):
            return f'{value:.6g}' if isinstance(value, float) else str(value) if value is not None else '-'
        for row in self.data['phase_a']:
            text.append('| ' + ' | '.join(val(row.get(k)) for k in
                ['program', 'frames', 'threads', 'repeat', 'wall_s', 'cpu_s', 'realtime_factor', 'xt_cpu_percent', 'latency_samples']) + ' |')
        text += ['', '## CamillaDSP file-mode processing load', '',
                 'GetProcessingLoad is a pipeline capacity estimate during unpaced file processing, not realtime endpoint CPU utilization.', '',
                 '| Chunk | Threads | Positive samples | Mean % | Min % | Max % |', '|---:|---|---:|---:|---:|---:|']
        for row in self.data['phase_a']:
            if row['program'] == 'CamillaDSP':
                load = row.get('processing_load') or {}
                cells = [row['frames'], row['threads']] + [load.get(key) for key in ['positive_samples', 'mean_percent', 'min_percent', 'max_percent']]
                text.append('| ' + ' | '.join(val(cell) for cell in cells) + ' |')
        omni_rows = [row for row in self.data['phase_a'] if row['program'] == 'Omniphony']
        if omni_rows:
            text += ['', 'Omniphony common leading samples trimmed (explicit loader log only): ' +
                     ', '.join(val(row.get('leading_samples_trimmed')) for row in omni_rows) +
                     '. A dash means the loader did not report a count; the shorter tap count is not used as a substitute.']
        text += ['', '## XT batch processing times', '', '| Batch | Threads | Repeat | Min us | Median us | Mean us | p95 us | p99 us | Max us |',
                 '|---:|---|---:|---:|---:|---:|---:|---:|---:|']
        for row in self.data['phase_a']:
            if row['program'] == 'XT':
                cells = [row['frames'], row['threads'], row['repeat']] + [row['batch_us'][key] for key in ['min', 'median', 'mean', 'p95', 'p99', 'max']]
                text.append('| ' + ' | '.join(val(cell) for cell in cells) + ' |')
        if 'feed' in self.data:
            text += ['', '## Upmixed feed', '', f"Reported plugin latency: {self.data['feed']['latency_samples']} samples.", '',
                     '| Slot (zero based) | Standard position | RMS | Original stereo-device RMS error |', '|---:|---|---:|---:|']
            names = ['FL', 'FR', 'FC', 'LFE', 'BL', 'BR', 'SL', 'SR', 'TFL', 'TFR', 'TBL', 'TBR']
            feed = self.data['feed']
            errors = feed.get('stereo_device_oracle_rms_error', [None] * 12)
            for i, rms in enumerate(feed.get('rms_per_channel', [])):
                text.append(f'| {i} | {names[i]} | {val(rms)} | {val(errors[i])} |')
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
        if 'baseline' in self.data:
            baseline = self.data['baseline']
            text += ['', f"Baseline L0 median: {val(baseline['median_samples'])} samples; spread: {baseline['spread_samples']} samples. Baseline rows show absolute lag, including the fixed preroll."]
        if 'cable' in self.data:
            text += ['', '## Cable endpoint mix formats', '', '| Snapshot | Endpoint | Flow | Channels | Rate | Bits |', '|---|---|---|---:|---:|---:|']
            for snapshot in ['install', 'ready']:
                for endpoint in self.data['cable'].get(snapshot, {}).get('endpoints', []):
                    fmt = endpoint.get('format') or {}
                    cells = [snapshot, endpoint['name'], endpoint['flow'], fmt.get('channels'), fmt.get('rate'), fmt.get('bits')]
                    text.append('| ' + ' | '.join(val(cell) for cell in cells) + ' |')
        if 'realtime_details' in self.data:
            text += ['', '## Realtime clean-run evidence', '', '| Run | Callback under/overflows | Unexpected zero gaps | Residual spike events | Residual dB | XT mute reports |',
                     '|---|---:|---:|---:|---:|---:|']
            for name, details in self.data['realtime_details'].items():
                if 'analysis' not in details:
                    continue
                analysis = details['analysis']
                cells = [name, sum(details['callback_flags'].values()), analysis['zero_gap_events'], analysis['residual_spike_events'],
                         analysis['residual_db'], len(details.get('program_diagnostics', {}).get('mute_reports', []))]
                text.append('| ' + ' | '.join(val(cell) for cell in cells) + ' |')
            text += ['', 'Clean requires no active-music callback under/overflow, no unexpected zero gap longer than 1 ms, and no 10 ms residual block above max(-20 dB, ten times the median residual power ratio). For baseline/XT it also requires residual below -30 dB and no XT mute/passthrough diagnostic. Startup/tail flags remain in results.json.']
        if 'multichannel' in self.data:
            text += ['', '## Multichannel cable attempts', '', self.data['multichannel']['second_cable_blocker'], '',
                     '| Requested channels | Render set HRESULT | Capture set HRESULT | Final render channels | Final capture channels | Probe exit | All channels verified |',
                     '|---:|---:|---:|---:|---:|---:|---|']
            for attempt in self.data['multichannel']['attempts']:
                results = attempt['format_results']
                endpoints = {row['name']: row for row in attempt.get('snapshot', {}).get('endpoints', [])}
                counts = [(endpoints.get(name, {}).get('format') or {}).get('channels') for name in ['CABLE Input', 'CABLE Output']]
                cells = [attempt['channels_requested']] + [results[i].get('set_hresult') if i < len(results) else None for i in range(2)] + counts + [attempt.get('probe_exit'), attempt.get('all_channels_delivered')]
                text.append('| ' + ' | '.join(val(cell) for cell in cells) + ' |')
        if 'omniphony_realtime' in self.data:
            text += ['', '## Omniphony realtime evidence', '',
                     '| Target ms | Capture lag samples vs XT | Source epoch correction samples | Gain vs XT | Residual vs XT dB | Own offline peak | Recorded peak | Recording spike events vs own offline output | Zero gaps | Program warnings |',
                     '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
            for details in self.data['omniphony_realtime'].values():
                if 'analysis' not in details:
                    continue
                analysis, clean_analysis = details['analysis'], details['clean_analysis']
                cells = [details['latency_target_ms'] if details['latency_target_ms'] is not None else 'unset', analysis['lag_samples'],
                         details['source_epoch_offset_samples'], analysis['gain'], analysis['residual_db'],
                         clean_analysis['reference_peak'], clean_analysis['recording_peak'],
                         clean_analysis['residual_spike_events'], clean_analysis['zero_gap_events'], sum(details['program_warning_counts'].values())]
                text.append('| ' + ' | '.join(val(cell) for cell in cells) + ' |')
            text += ['', 'Program counter values are null when no explicit counter was logged, not assumed zero. Full numeric callback, counter and warning evidence is in results.json. Startup-inclusive added delay is not a measurement of steady-state DSP buffering. Residual spikes alone are unconfirmed dropout candidates: an offline peak above full scale and a bounded recording can also indicate level limiting; no gain is tuned to remove that difference.']
        if 'frontends' in self.data:
            text += ['', '## Frontend settings observation', '', 'Only filenames and key names follow. No settings values, plugin state, or GUI automation were used.', '']
            for name, observation in self.data['frontends'].items():
                text += ['### ' + name, '', '| File | Key names |', '|---|---|']
                for row in observation['changed_files'] + observation.get('preexisting_vendor_settings', []):
                    if any(token in row['file'].lower() for token in ['zauro', 'zsonic', 'multichannel', 'juce', '.settings']):
                        text.append('| ' + row['file'] + ' | ' + ', '.join(row['key_names']) + ' |')
                for field in ['audio_device_keys', 'channel_count_keys', 'plugin_keys']:
                    text.append(field + ': ' + (', '.join(observation[field]) or 'none observed'))
                text += ['', observation['conclusion'], '']
        text += ['', '## Step status', ''] + [f'- {key}: {value}' for key, value in self.data['steps'].items()]
        text += ['', '## Measurement definitions and limitations', ''] + ['- ' + x for x in self.data['notes']]
        (self.summary / 'results.md').write_text('\n'.join(text) + '\n', encoding='ascii', errors='backslashreplace')

    def run(self, label, command, timeout=300, cwd=None, allowed=(0,), load_port=None):
        # stdout/stderr files prevent pipe deadlocks and keep arbitrary vendor
        # output, including any echoed state, out of the public runner log.
        start = time.perf_counter()
        with (self.work / (label + '.stdout.log')).open('wb') as out, (self.work / (label + '.stderr.log')).open('wb') as err:
            p = subprocess.Popen([str(x) for x in command], cwd=cwd or self.args.workspace,
                                 stdout=out, stderr=err)
            poller = LoadPoller(load_port) if load_port else None
            try:
                p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
                p.wait(timeout=10)
                raise MeasurementError(label + ' exceeded its process timeout.') from None
            finally:
                wall = time.perf_counter() - start
                load = poller.finish() if poller else None
            cpu = cpu_seconds(p)
        if p.returncode not in allowed and label == 'Omniphony-bridge-build':
            # Compiler diagnostics cannot contain private inputs: the build
            # never receives Inputs or any derived config/audio path.
            errors = (self.work / (label + '.stderr.log')).read_text(errors='replace')
            self.data['omniphony']['build_errors'] = [line[:400] for line in errors.splitlines()
                                                       if line.startswith(('error:', 'warning:'))][:12]
            self.save()
        require(p.returncode in allowed, label + ' failed (exit ' + str(p.returncode) + ').')
        result = {'wall_s': wall, 'cpu_s': cpu, 'exit_code': p.returncode}
        if load is not None:
            result['processing_load'] = load
        return result

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
                row = self.run(label, [self.camilla, '-p', '12341', '-a', '127.0.0.1',
                                      '-o', self.work / (label + '.program.log'), config], 600, load_port=12341)
                row.update({'program': 'CamillaDSP', 'frames': frames, 'threads': 'multi' if threaded else 'single',
                            'repeat': 1, 'realtime_factor': self.duration / row['wall_s']})
                audio = np.fromfile(output, dtype='<f8')
                require(len(audio) > RATE * 2 * 40 and len(audio) % 2 == 0 and np.isfinite(audio).all(), 'CamillaDSP output is missing, short, or invalid.')
                sf.write(self.work / (label + '.wav'), audio.reshape(-1, 2), RATE, subtype='FLOAT')
                self.data['phase_a'].append(row)
                self.save()
        self.data['steps']['A3'] = 'measured: all six file-throughput settings; GetProcessingLoad polled every 0.5 seconds on loopback'
        self.save()

    def compare(self, name, tested, reference, zero_lag=False, informational=False):
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
        if informational:
            row['verdict'] = 'measured (different BRIR preprocessing)'
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
        expected = 'ad2012377fbd0760794347d2b900f5a6508ab386d9fff22bb4e9a3e2c17fddbb'
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
repository = "https://github.com/mgth/Omniphony"
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
        self.omniphony_sofa(bridge)

    def omniphony_sofa(self, bridge):
        from netCDF4 import Dataset
        import struct
        samples, _ = sf.read(self.brir, always_2d=True)
        slots = [0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11]
        azimuths = [30, -30, 0, 150, -150, 90, -90, 45, -45, 135, -135]
        elevations = [0] * 7 + [45] * 4
        responses = np.empty((11, 2, len(samples)))
        for emitter, slot in enumerate(slots):
            for ear in range(2):
                rows = [i for i, item in enumerate(self.conv)
                        if item['feed'][slot] != 0 and self.final_matrix[ear][i] != 0]
                require(len(rows) == 1, 'SOFA conversion requires exactly one response per speaker and ear.')
                i = rows[0]
                responses[emitter, ear] = samples[:, self.conv[i]['index']] * self.conv[i]['feed'][slot] * self.final_matrix[ear][i]
        sofa = self.work / 'room.sofa'
        with Dataset(sofa, 'w', format='NETCDF4') as ds:
            for name, size in [('M', 11), ('R', 2), ('N', len(samples)), ('C', 3), ('I', 1), ('E', 1)]:
                ds.createDimension(name, size)
            for name, value in {'Conventions': 'SOFA', 'Version': '1.0', 'SOFAConventions': 'SimpleFreeFieldHRIR',
                                'SOFAConventionsVersion': '1.0', 'APIName': 'latency-benchmark', 'APIVersion': '1.0',
                                'DataType': 'FIR', 'RoomType': 'reverberant', 'Title': 'Private runner BRIR',
                                'License': 'Private; not redistributable'}.items():
                ds.setncattr(name, value)
            def variable(name, dims, values, coordinate=None):
                var = ds.createVariable(name, 'f8', dims)
                var[:] = values
                if coordinate:
                    var.Type, var.Units = coordinate
                return var
            variable('Data.IR', ('M', 'R', 'N'), responses)
            variable('Data.SamplingRate', ('I',), [RATE]).Units = 'hertz'
            variable('Data.Delay', ('I', 'R'), [[0, 0]])
            variable('SourcePosition', ('M', 'C'), np.column_stack([azimuths, elevations, np.full(11, 2)]),
                     ('spherical', 'degree, degree, metre'))
            variable('ListenerPosition', ('I', 'C'), [[0, 0, 0]], ('cartesian', 'metre'))
            variable('ListenerView', ('I', 'C'), [[1, 0, 0]], ('cartesian', 'metre'))
            variable('ListenerUp', ('I', 'C'), [[0, 0, 1]], ('cartesian', 'metre'))
            variable('ReceiverPosition', ('R', 'C', 'I'), np.array([[0, 0.09, 0], [0, -0.09, 0]])[:, :, None], ('cartesian', 'metre'))
            variable('EmitterPosition', ('E', 'C', 'I'), np.zeros((1, 3, 1)), ('cartesian', 'metre'))
        # Give the reference decoder an explicit WAVEFORMATEXTENSIBLE mask;
        # a generic twelve-channel WAV need not imply the same bed order.
        audio, _ = sf.read(self.feed, always_2d=True, dtype='float32')
        # Preserve the feed, including LFE. The measured material has silent
        # LFE; Omniphony's dry LFE policy is not changed to imitate XT.
        wave = self.work / 'omni-feed.wav'
        payload = audio.astype('<f4').tobytes()
        fmt = struct.pack('<HHIIHHHHI', 0xfffe, 12, RATE, RATE * 48, 48, 32, 22, 32, 0x2d63f)
        fmt += bytes.fromhex('0300000000001000800000aa00389b71')
        wave.write_bytes(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(payload)) +
                         b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(payload)) + payload)
        names = ['FL', 'FR', 'C', 'BL', 'BR', 'SL', 'SR', 'TFL', 'TFR', 'TBL', 'TBR']
        layout = {'speakers': [{'name': name, 'azimuth': -az, 'elevation': el, 'distance': 2,
                              'spatialize': True, 'gain_db': 0, 'delay_ms': 0}
                             for name, az, el in zip(names, azimuths, elevations)]}
        config = self.work / 'omni.yml'
        config.write_text(yaml.safe_dump({'render': {'current_layout': layout,
            'binaural': {'output_mode': 'binaural', 'hrir_source': 'brir', 'brir_sofa_path': str(sofa),
                         'brir_max_length_s': 0.0, 'brir_tail_floor_db': 200.0}}}), encoding='utf-8')
        self.data['omniphony']['sofa'] = {'convention': 'SimpleFreeFieldHRIR', 'emitters': 11,
                                        'receivers': 2, 'samples': len(samples), 'sample_rate': RATE,
                                        'azimuths_degrees': azimuths, 'elevations_degrees': elevations,
                                        'brir_max_length_s': 0, 'brir_tail_floor_db': 200}
        self.save()
        require(self.data['omniphony']['file_backend_in_installed_help'], 'Installed Omniphony has no file backend for a headless SOFA load check.')
        row = self.run('Omniphony-sofa-load', [self.orender, '--config', config, '--loglevel', 'info',
                        'render', wave, '--bridge-path', bridge, '--output-backend', 'file',
                        '--output-file', self.work / 'omni-load.raw', '--output-file-format', 'raw-f32'], 300)
        logs = ''.join((self.work / ('Omniphony-sofa-load.' + stream + '.log')).read_text(errors='replace')
                       for stream in ['stdout', 'stderr'])
        loaded = re.search(r'BRIR: .*?(\d+) emitters.*?(\d+) orientations, up to (\d+) taps', logs)
        self.data['omniphony']['sofa_load'] = row
        self.data['omniphony']['sofa_load']['confirmation_found'] = bool(loaded)
        if loaded:
            self.data['omniphony']['sofa_load'].update({'emitters': int(loaded[1]), 'orientations': int(loaded[2]), 'taps': int(loaded[3])})
        self.save()
        require(loaded is not None and int(loaded[1]) == 11, 'Omniphony did not confirm loading the eleven-emitter SOFA; fallback is not accepted.')
        self.data['steps']['A5'] = 'measured: reference bridge built, check-bridges passed, eleven-emitter SOFA loaded via the installed file backend'
        self.data['notes'].append('Installed Omniphony v0.7.0 supports a non-realtime file output backend, used for BRIR validation and offline throughput. Offline throughput is not a realtime latency or dropout measurement.')
        self.data['notes'].append('Omniphony source removes common BRIR lead silence, fades tails and normalizes direct-sound energy; its future latency/residual comparison must account for those transformations.')
        self.save()

    def phase_a6(self):
        self.data['notes'].append('Omniphony offline settings: binaural BRIR, SimpleFreeFieldHRIR, 48000 Hz, eleven standard-position speakers, brir_max_length_s=0 (unbounded), brir_tail_floor_db=200, raw-f32 file output. These are the existing A5 settings; no residual-driven tuning was done.')
        self.data['notes'].append('LFE handling differs: Omniphony feeds LFE dry at -3 dB to both ears; XT folds LFE into the centre convolution. LFE RMS is zero in this material; the original twelve-channel feed is preserved.')
        self.data['notes'].append('The documented Omniphony BRIR convolution delay is 127 samples. Measured output lag also includes its common-lead removal and other processing; 127 is not substituted for a measurement.')
        for rep in range(1, 4):
            label = f'Omniphony-offline-{rep}'
            raw = self.work / (label + '.raw')
            row = self.run(label, [self.orender, '--config', self.work / 'omni.yml', '--loglevel', 'info',
                            'render', self.work / 'omni-feed.wav', '--bridge-path',
                            self.work / 'omni-target/release/reference_bridge.dll', '--output-backend', 'file',
                            '--output-file', raw, '--output-file-format', 'raw-f32'], 600)
            logs = ''.join((self.work / (label + '.' + stream + '.log')).read_text(errors='replace')
                           for stream in ['stdout', 'stderr'])
            require(re.search(r'BRIR: .*?11 emitters.*?1 orientations', logs) is not None,
                    'Omniphony did not confirm BRIR loading during the measured render.')
            samples = np.fromfile(raw, dtype='<f4')
            require(len(samples) % 2 == 0 and len(samples) > RATE * 80 and np.isfinite(samples).all(),
                    'Omniphony file output is too short or invalid.')
            wave = self.work / (label + '.wav')
            sf.write(wave, samples.reshape(-1, 2), RATE, subtype='FLOAT')
            row.update({'program': 'Omniphony', 'frames': None, 'threads': 'default', 'repeat': rep,
                        'realtime_factor': self.duration / row['wall_s'], 'output_frames': len(samples) // 2,
                        'leading_samples_trimmed': None,
                        'leading_trim_reason': 'Not reported in the BRIR loader log; not inferred from the shortened tail or correlation lag.'})
            # Only accept an explicit count, not a derived difference in taps.
            trim = re.search(r'(?:trimmed|removed)\s+(\d+)\s+(?:leading|lead)\s+samples', logs, re.I)
            if trim:
                row['leading_samples_trimmed'] = int(trim[1])
                row['leading_trim_reason'] = 'Explicit loader log count.'
            self.data['phase_a'].append(row)
            self.save()
            self.compare(f'Omniphony {rep} vs XT', wave, self.work / 'XT-480-1.wav', informational=True)
        self.data['steps']['A6'] = 'measured: three file renders with wall/CPU time, lag, one gain and residual; different BRIR preprocessing is reported, not tuned away'
        self.save()

    def cable_action(self, label, mode, endpoint=None, channels=2):
        output = self.work / (label + '.json')
        command = ['pwsh', '-NoProfile', '-File', Path(__file__).with_name('Cable-Endpoint.ps1'),
                   '-Mode', mode, '-Work', self.work, '-OutputJson', output]
        if endpoint is not None:
            command += ['-EndpointId', endpoint, '-Channels', str(channels), '-Rate', str(RATE)]
        self.run(label, command, 600 if mode == 'Install' else 120)
        return json.loads(output.read_text(encoding='utf-8-sig'))

    def phase_b0(self):
        setup = self.cable_action('cable-install', 'Install')
        self.data['cable'] = {'install': setup, 'format_attempts': []}
        self.data['downloads']['VB-CABLE'] = {'version': 'Pack43', 'sha256': setup['sha256']}
        self.save()
        endpoints = setup['endpoints']
        for name in ['CABLE Input', 'CABLE Output']:
            found = [x for x in endpoints if x['name'] == name]
            require(len(found) == 1, 'VB-CABLE did not provide both explicitly named active endpoints.')
            row = self.cable_action('cable-format-' + ('render' if name == 'CABLE Input' else 'capture'),
                                    'SetFormat', found[0]['id'])
            self.data['cable']['format_attempts'].append(row)
            self.save()
        snapshot = self.cable_action('cable-ready', 'Snapshot')
        self.data['cable']['ready'] = snapshot
        self.endpoints = {row['name']: row for row in snapshot['endpoints']}
        for name in ['CABLE Input', 'CABLE Output']:
            fmt = self.endpoints[name]['format']
            require(fmt is not None and fmt['channels'] == 2 and fmt['rate'] == RATE,
                    'Cable endpoint did not accept stereo 48000 Hz shared mix format; no converted-rate substitute is used.')
        self.data['steps']['B0'] = 'implemented: explicit-name WASAPI shared duplex tool and aligned residual/zero-gap analysis; actual streaming is verified by B1'
        self.data['notes'].append('Realtime playback first warms the same open duplex stream with 15 seconds of silence, discarded before analysis, then uses a fixed 24000-frame preroll included equally in L0 and processed recordings. audiodg CPU covers the whole 107.5-second stream, including warmup and tail. Neither endpoint is selected through a default-device API.')
        self.data['notes'].append('B2 added delay is the recording lag against XT A1 output minus L0; the offline reference already includes the plugin delay, so no further 4351-sample subtraction is made.')
        self.save()

    def audio_round(self, label, reference, program):
        output = self.work / (label + '.json')
        log = Path(r'C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log')
        offset = log.stat().st_size if log.exists() else 0
        command = [sys.executable, '-I', Path(__file__).with_name('audio_io.py'),
                   '--play', self.music, '--record', self.work / (label + '.wav'), '--reference', reference,
                   '--render-name', 'CABLE Input', '--capture-name', 'CABLE Output', '--json', output]
        process = self.run(label, command, 150, allowed=(0, 1))
        details = json.loads(output.read_text(encoding='ascii'))
        self.data.setdefault('realtime_details', {})[label] = details
        self.save()
        require(process['exit_code'] == 0, 'WASAPI audio tool failed; numeric PortAudio error is in realtime_details.')
        time.sleep(3)
        diagnostics = {'log_present': log.exists(), 'mute_reports': [], 'passthrough_warnings': 0}
        if program == 'XT' and log.exists():
            with log.open('rb') as stream:
                stream.seek(offset)
                text = stream.read().decode('utf-8-sig', errors='replace')
            diagnostics['mute_reports'] = [[int(x) for x in match] for match in
                re.findall(r'(\d+) differs from initialized (\d+); output muted.*?\[mute calls: (\d+) total\]', text)]
            diagnostics['passthrough_warnings'] = text.lower().count('passing audio through')
        details['program_diagnostics'] = diagnostics
        analysis = details['analysis']
        reference_valid = analysis['residual_power_ratio'] < 0.001
        program_log_available = program != 'XT' or diagnostics['log_present']
        clean = details['clean'] and reference_valid and program_log_available and not diagnostics['mute_reports'] and not diagnostics['passthrough_warnings']
        dropouts = bool(analysis['recording_dropouts'] or any(details['callback_flags'].values()) or diagnostics['mute_reports'])
        row = {'program': program, 'setting': label, 'lag_samples': analysis['lag_samples'],
               'added_samples': None, 'added_ms': None, 'dropouts': dropouts,
               'program_log_available': program_log_available,
               'cpu_s': details['audiodg_cpu_s'], 'audio_tool_cpu_s': process['cpu_s'],
               'gain': analysis['gain'], 'residual_db': analysis['residual_db'],
               'reference_match_below_minus30db': reference_valid,
               'clean': clean, 'clean_definition': details['clean_definition'] +
                   ' Also requires residual below -30dB for this identity-equivalent chain, no XT mute report and no XT passthrough warning.'}
        self.data['phase_b'].append(row)
        self.save()
        return row

    def phase_b1(self):
        rows = [self.audio_round(f'baseline-{rep}', self.music, 'Cable baseline') for rep in range(1, 4)]
        lags = [row['lag_samples'] for row in rows]
        self.l0 = float(np.median(lags))
        self.data['baseline'] = {'lag_samples': lags, 'median_samples': self.l0,
                                 'spread_samples': max(lags) - min(lags), 'all_clean': all(row['clean'] for row in rows)}
        for row in rows:
            row['added_samples'] = row['lag_samples']
            row['added_ms'] = row['lag_samples'] * 1000 / RATE
        self.data['steps']['B1'] = 'measured: three baseline runs; table shows absolute reference lag (L0), including the fixed preroll'
        self.save()
        if not all(row['clean'] for row in rows):
            self.data['notes'].append('The cable baseline is not clean. Later lag-minus-L0 values are arithmetic observations with an invalid baseline, not an isolated processing-delay estimate; they cannot establish a lowest clean setting.')
            self.save()

    def phase_b2(self):
        self.config_dir.mkdir(exist_ok=True, parents=True)
        for file in self.full_config.iterdir():
            shutil.copyfile(file, self.config_dir / file.name)
        # Trace remains on the disposable runner. Never copy this log: it can
        # contain private configuration. Export only numeric diagnostic counts.
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\EqualizerAPO', 0, winreg.KEY_SET_VALUE) as key:
            winreg.SetValueEx(key, 'EnableTrace', 0, winreg.REG_SZ, 'true')
        selector = self.current / 'DeviceSelector.exe'
        guid = self.endpoints['CABLE Input']['guid']
        self.data['xt_apo'] = {'flow': 'Render', 'endpoint': 'CABLE Input', 'guid': guid}
        try:
            install = self.run('XT-install-render-APO', [selector, '--install-endpoint', guid], 300, self.current, allowed=(0, 1))
            self.data['xt_apo']['install'] = install
            self.save()
            require(install['exit_code'] == 0, 'DeviceSelector did not confirm the render-endpoint APO install.')
            time.sleep(3)
            for rep in range(1, 4):
                row = self.audio_round(f'XT-realtime-{rep}', self.work / 'XT-480-1.wav', 'XT')
                row['added_samples'] = row['lag_samples'] - self.l0
                row['added_ms'] = row['added_samples'] * 1000 / RATE
                row['added_delay_valid'] = self.data['baseline']['all_clean'] and row['clean']
                self.save()
            self.data['steps']['B2'] = 'measured: three render-APO runs, offline XT reference minus baseline median, audiodg CPU and recording/program diagnostics'
        finally:
            self.data['xt_apo']['uninstall'] = self.run('XT-uninstall-render-APO',
                [selector, '--uninstall-endpoint', guid], 180, self.current, allowed=(0, 1))
            (self.config_dir / 'config.txt').write_text('Preamp: 0 dB\n', encoding='ascii')
            self.save()
            require(self.data['xt_apo']['uninstall']['exit_code'] == 0, 'XT render APO could not be uninstalled; later program measurements must not use this endpoint.')

    def phase_b3(self):
        # The official page exposes only donation/account-specific links, not
        # a public A+B archive. Do not guess protected download URLs or replace
        # the requested two-cable topology with a channel-sharing shortcut.
        self.data['multichannel'] = {'source': 'https://vb-audio.com/Cable/VirtualCables.htm',
            'second_cable_blocker': 'Official A+B pack requires a personal donation/account download link. No such link or authorized archive was provided; CABLE-A and CABLE-B are unavailable.',
            'attempts': []}
        try:
            for channels in [12, 16]:
                attempt = {'channels_requested': channels, 'format_results': []}
                self.data['multichannel']['attempts'].append(attempt)
                for name in ['CABLE Input', 'CABLE Output']:
                    attempt['format_results'].append(self.cable_action(f'cable-{channels}-' + ('render' if name == 'CABLE Input' else 'capture'),
                        'SetFormat', self.endpoints[name]['id'], channels))
                snapshot = self.cable_action(f'cable-{channels}-snapshot', 'Snapshot')
                attempt['snapshot'] = snapshot
                available = {row['name']: row for row in snapshot['endpoints']}
                accepted = all(available[name]['format'] is not None and
                    available[name]['format']['channels'] == channels and available[name]['format']['rate'] == RATE
                    for name in ['CABLE Input', 'CABLE Output'])
                attempt['mix_formats_accepted'] = accepted
                self.save()
                if not accepted:
                    attempt['probe'] = 'not measured: both shared mix formats did not accept the requested channel count'
                    continue
                # Independent deterministic signals prove delivery in every
                # channel; these are synthetic, not any private input.
                rng = np.random.default_rng(714)
                audio = rng.standard_normal((45 * RATE, channels)).astype(np.float32) * 0.01
                audio = signal.sosfilt(signal.butter(4, 4000, fs=RATE, output='sos'), audio, axis=0)
                wave = self.work / f'channel-probe-{channels}.wav'
                sf.write(wave, audio, RATE, subtype='FLOAT')
                result = self.work / f'channel-probe-{channels}.json'
                process = self.run(f'channel-probe-{channels}', [sys.executable, '-I', Path(__file__).with_name('audio_io.py'),
                    '--play', wave, '--record', self.work / f'channel-probe-{channels}-recorded.wav', '--reference', wave,
                    '--render-name', 'CABLE Input', '--capture-name', 'CABLE Output', '--channels', str(channels), '--json', result], 120, allowed=(0, 1))
                attempt['probe'] = json.loads(result.read_text(encoding='ascii'))
                attempt['probe_exit'] = process['exit_code']
                attempt['all_channels_delivered'] = process['exit_code'] == 0 and attempt['probe']['analysis']['residual_power_ratio'] < 0.001
                self.save()
        finally:
            for name in ['CABLE Input', 'CABLE Output']:
                self.cable_action('cable-restore-' + ('render' if name == 'CABLE Input' else 'capture'),
                                  'SetFormat', self.endpoints[name]['id'], 2)
            snapshot = self.cable_action('cable-restored', 'Snapshot')
            self.data['multichannel']['restored'] = snapshot
            self.endpoints = {row['name']: row for row in snapshot['endpoints']}
            self.save()
            require(all(self.endpoints[name]['format'] is not None and self.endpoints[name]['format']['channels'] == 2 and
                        self.endpoints[name]['format']['rate'] == RATE for name in ['CABLE Input', 'CABLE Output']),
                    'Cable stereo format could not be restored after multichannel probing.')
        self.data['steps']['B3'] = 'not measured: no authorized A+B download or second cable; standard-cable 12/16-channel format/probe attempts are recorded, but no alternate topology was substituted'
        self.data['notes'].append('No CamillaDSP lowest clean realtime setting exists in this run: the two-cable topology could not be assembled. Hosted-runner CPU is stated above; no result is generalized to a faster machine.')
        self.save()

    def phase_b4(self):
        require((self.work / 'Omniphony-offline-1.wav').exists(), 'No Omniphony offline reference is available for realtime dropout analysis.')
        require(self.data.get('xt_apo', {}).get('uninstall', {}).get('exit_code') == 0,
                'Omniphony realtime requires the XT APO to be uninstalled first.')
        require(all(self.endpoints[name]['format']['channels'] == 2 and self.endpoints[name]['format']['rate'] == RATE
                    for name in ['CABLE Input', 'CABLE Output']), 'Omniphony requires restored stereo cable formats.')
        self.run('Omniphony-devices', [self.orender, 'list-asio-devices'], 60)
        device_text = (self.work / 'Omniphony-devices.stdout.log').read_text(errors='replace')
        require(re.search(r'^Output host:\s*WASAPI', device_text, re.M | re.I) is not None,
                'Omniphony did not select its WASAPI fallback host; no default or different backend was substituted.')
        names = re.findall(r'^\s*\d+\.\s+(CABLE Input(?: \([^\r\n]+\))?)\s*$', device_text, re.M)
        require(len(names) == 1, 'Omniphony did not enumerate one exact CABLE Input target.')
        self.data['omniphony']['realtime_device'] = names[0]
        self.data['notes'].append('Omniphony realtime uses the process-start request as its file-source epoch, interpolated from capture callbacks (10 ms callback-clock resolution). Its lag-minus-L0 includes CLI initialization and BRIR preprocessing; it is not an isolated steady-state DSP-buffer delay. No silent extra render stream or alternate cable topology is used.')
        for target in [None, 10]:
            label = 'Omniphony-realtime-' + ('unset' if target is None else str(target))
            config = yaml.safe_load((self.work / 'omni.yml').read_text())
            if target is not None:
                config['render']['latency_target'] = target
            path = self.work / (label + '.yml')
            path.write_text(yaml.safe_dump(config), encoding='utf-8')
            command = [str(self.orender), '--config', str(path), '--loglevel', 'info', '--log-format', 'json',
                'render', str(self.work / 'omni-feed.wav'), '--bridge-path', str(self.work / 'omni-target/release/reference_bridge.dll'),
                '--output-backend', 'asio', '--output-device', names[0]]
            command_file = self.work / (label + '-command.json')
            command_file.write_text(json.dumps(command), encoding='utf-8')
            output = self.work / (label + '.json')
            process = self.run(label + '-capture', [sys.executable, '-I', Path(__file__).with_name('omni_realtime.py'),
                '--command', command_file, '--capture-name', 'CABLE Output', '--record', self.work / (label + '.wav'),
                '--reference', self.work / 'XT-480-1.wav', '--clean-reference', self.work / 'Omniphony-offline-1.wav',
                '--json', output, '--log-prefix', self.work / label, '--workspace', self.args.workspace], 170, allowed=(0, 1))
            details = json.loads(output.read_text(encoding='ascii'))
            self.data.setdefault('omniphony_realtime', {})[label] = details
            logs = ''.join((self.work / (label + '.' + stream + '.log')).read_text(errors='replace')
                           for stream in ['stdout', 'stderr'] if (self.work / (label + '.' + stream + '.log')).exists())
            counters = {}
            for key in ['frames_lost', 'frames_filled', 'realigns', 'underruns']:
                values = re.findall(r'\b' + key + r'\b["\s]*[:=]["\s]*(\d+)', logs)
                counters[key] = max(int(value) for value in values) if values else None
            warnings = {}
            for pattern in ['underrun', 'realign', 'frames lost', 'frames filled', 'buffer overrun', 'clock mismatch']:
                warnings[pattern] = sum(pattern in line.lower() and re.search(r'warn|error', line, re.I) is not None for line in logs.splitlines())
            details['program_counters'] = counters
            details['program_warning_counts'] = warnings
            details['latency_target_ms'] = target
            details['brir_load_confirmed'] = re.search(r'BRIR: .*?11 emitters.*?1 orientations', logs) is not None
            self.save()
            require(process['exit_code'] == 0, 'Omniphony realtime capture failed; available numeric producer diagnostics were retained.')
            require(details['brir_load_confirmed'], 'Omniphony realtime did not confirm BRIR loading; fallback output is not accepted.')
            require(details['producer_wall_s'] >= 80, 'Omniphony did not render this file at realtime pace.')
            analysis = details['analysis']
            lag = analysis['lag_samples'] - details['source_epoch_offset_samples']
            reported_dropouts = bool(any(details['callback_flags'].values()) or details['clean_analysis']['zero_gap_events'] or
                                     any(warnings.values()) or any(value for value in counters.values() if value is not None))
            residual_candidates = details['clean_analysis']['residual_spike_events']
            dropouts = True if reported_dropouts else ('unconfirmed residual spikes' if residual_candidates else False)
            details['dropout_interpretation'] = 'Residual spikes alone do not prove lost frames; limiter/clipping differences can also cause them. Counter omissions remain unknown.'
            if details['clean_analysis']['reference_peak'] > 1 and details['clean_analysis']['recording_peak'] <= 1:
                details['level_observation'] = 'Own offline output exceeds full scale while the recording is bounded below full scale. Level limiting is a possible cause of residual spikes, not a proven dropout cause. No gain was tuned.'
            row = {'program': 'Omniphony (startup-inclusive)', 'setting': 'latency_target=' + ('unset' if target is None else str(target)),
                   'lag_samples': analysis['lag_samples'], 'source_epoch_corrected_lag_samples': lag,
                   'added_samples': lag - self.l0, 'added_ms': (lag - self.l0) * 1000 / RATE,
                   'dropouts': dropouts, 'cpu_s': details['producer_cpu_s'], 'gain': analysis['gain'],
                   'residual_db': analysis['residual_db'], 'clean': not dropouts,
                   'added_delay_valid': False,
                   'added_delay_limitation': 'Startup-inclusive source epoch, not independently timestamped first decoded audio; cannot establish steady-state DSP buffering.'}
            self.data['phase_b'].append(row)
            self.save()
        self.data['steps']['B4'] = 'measured: WASAPI file playback at unset and 10 ms targets, CPU/recording/log diagnostics and startup-inclusive lag; isolated steady-state added delay not measured'
        self.save()

    def phase_b5(self):
        self.run('install-MultiChannel-host', [self.inputs / 'MultiChannelPluginHost-1.1.1-Windows-x64-Setup.exe',
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-'], 300)
        self.data['frontends'] = {}
        programs = {'MultiChannel Plugin Host': Path(r'C:\Program Files\MultiChannel Plugin Host\MultiChannel Plugin Host.exe'),
                    'zAuro standalone': Path(r'C:\Program Files\zSonic\zAuro\zAuro Pro 2D-3D Upmixer.exe')}
        for index, (name, executable) in enumerate(programs.items()):
            label = f'frontend-{index}'
            output = self.work / (label + '.json')
            require(executable.exists(), 'A frontend executable was not found at its established installer location.')
            self.run(label, [sys.executable, '-I', Path(__file__).with_name('frontend_settings.py'), '--exe', executable,
                     '--work', self.work, '--output', output, '--label', label], 150)
            self.data['frontends'][name] = json.loads(output.read_text(encoding='ascii'))
            self.save()
        self.data['steps']['B5'] = 'investigated: each frontend ran for up to five seconds and was terminated; only changed filenames and XML/JSON/registry key names were retained; no UI automation or settings-value export'
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
        for name, action in [('prepare', self.prepare), ('A1', self.phase_a1), ('A2', self.phase_a2), ('A3', self.phase_a3), ('A4', self.phase_a4), ('A5', self.phase_a5), ('A6', self.phase_a6), ('B0', self.phase_b0), ('B1', self.phase_b1), ('B2', self.phase_b2), ('B3', self.phase_b3), ('B4', self.phase_b4), ('B5', self.phase_b5)]:
            if name in ('A6', 'B4') and not self.data['steps']['A5'].startswith('measured:'):
                self.data['steps'][name] = 'not measured: Omniphony bridge or SOFA validation in A5 did not complete'
                self.save()
                continue
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
                self.data.setdefault('phase_failures', {})[name] = {'reason': reason, 'locations': self.data['diagnostic_locations']}
                self.save()
                if name in ('A5', 'A6', 'B3', 'B4', 'B5'):
                    # Omniphony is optional: a bridge/build/render failure
                    # must not prevent the independent cable and XT rounds.
                    continue
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
