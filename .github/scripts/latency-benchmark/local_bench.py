"""Local measurement entry point; never called by the hosted workflow.

No installation, device-policy/default-device changes, or live-config writes.
Only the coordinator runs this file. stdout is numeric JSON except the explicitly
requested device-name inventory. Child stdout/stderr stays below --work.

Extra dependency: netCDF4 is required only when preparing Omniphony's SOFA.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import sys
import threading
import time
import urllib.request
import uuid
import winreg
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from local_support import Child, LocalFailure, OscMetrics, asio_registry_counts, audiodg_times, check, cpu_time, endpoint_mix, free_port, inside, reserve_control_port, sounddevice_module, timer_resolution
from camilla_load import LoadPoller

RATE = 48000
PEAK = 0.25
RENDER_16 = 'CABLE In 16ch (VB-Audio Virtual Cable)'
RENDER_8 = 'CABLE Input (VB-Audio Virtual Cable)'
CAPTURE = 'CABLE Output (VB-Audio Virtual Cable)'
EXPECTED = {RENDER_16: ('max_output_channels', 16), RENDER_8: ('max_output_channels', 8), CAPTURE: ('max_input_channels', 16)}
FORBIDDEN = ('topping', 'realphones', 'steam', 'nvidia')
CAMILLA_URL = 'https://github.com/HEnquist/camilladsp/releases/download/v4.1.3/camilladsp-windows-amd64.zip'
CAMILLA_SHA = 'd949de5ac5fc13282dd47155e0ac67087c09dd84baf7c7d568616bfc6e43d190'
SOURCE_SHA = 'ad2012377fbd0760794347d2b900f5a6508ab386d9fff22bb4e9a3e2c17fddbb'
CONFIG_SHA = '49dfbc4d9a2fa664d6135d0b07de2dbbd1a626b86ff1c88b0da20a985110f5ff'
ERRORS = {
    10: 'Invalid command-line arguments.',
    100: 'Windows and a non-runner process are required.', 101: 'Elevated execution is forbidden.',
    102: 'An output path is outside --work.', 103: 'Work overlaps a repository, inputs, installation, or live config.',
    104: 'A required Python dependency is missing; netCDF4 is needed for SOFA preparation.',
    105: 'A prerequisite step or supplied executable/reference is missing.',
    110: 'An exact required WASAPI endpoint is missing or ambiguous; do not substitute a device.',
    111: 'A forbidden or non-allowlisted endpoint was requested.', 112: 'The endpoint channel count/rate differs from the declared mix format.',
    120: 'The Windows owned-process job could not be established.', 121: 'Executable is not an approved command-line program.',
    122: 'An owned child exceeded its deadline.', 123: 'An owned process showed a visible window and was stopped.',
    124: 'A child exited nonzero; only its exit number is public.', 125: 'Owned-process cleanup could not be verified.', 130: 'A hash check failed.', 131: 'Unsafe archive entry.',
    140: 'BASELINE IS NOT CLEAN/STABLE: spread exceeds 48 samples (1 ms), recording/timestamps failed, or the timing/input signature differs. Do not proceed.',
    141: 'An audio stream ended early or exceeded its deadline.', 142: 'Stream timestamp alignment was not usable/stable.',
    150: 'A playback/reference signal exceeds the -12 dBFS safety ceiling.', 151: 'Audio duration is insufficient.',
    160: 'Wire/channel-order or no-feedback check failed.', 170: 'A fresh Remove marker from Local-ConfigSwap is required.',
    171: 'Restore the live config before baseline/XT measurement.', 180: 'No clean realtime setting was found.',
    181: 'Omniphony probes registered ASIO drivers before WASAPI fallback; local ASIO registrations prevent a safe realtime launch under the hardware restrictions.',
    190: 'The installed product path differs from the explicitly approved per-user installation.',
    199: 'Interrupted; owned process jobs were closed. The coordinator must still run Restore.',
    900: 'Unexpected failure; only exception type and source locations are retained.'}


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False), encoding='ascii')
    temporary.replace(path)


def modules():
    try:
        import numpy as np
        sd = sounddevice_module()
        import soundfile as sf
        import scipy.signal as signal
        from audio_io import analyse
        from benchmark import Bench
        return np, sd, sf, signal, analyse, Bench
    except ModuleNotFoundError:
        raise LocalFailure(104) from None


def device_inventory():
    sd = sounddevice_module()
    found, selected = [], {}
    for index, item in enumerate(sd.query_devices()):
        if sd.query_hostapis(item['hostapi'])['name'] != 'Windows WASAPI':
            continue
        allowed = item['name'] in EXPECTED and not any(word in item['name'].lower() for word in FORBIDDEN)
        found.append({'name': item['name'], 'index': index, 'input_channels': item['max_input_channels'],
                      'output_channels': item['max_output_channels'], 'rate': item['default_samplerate'], 'allowed': int(allowed)})
    for name, (key, count) in EXPECTED.items():
        column = 'input_channels' if key == 'max_input_channels' else 'output_channels'
        candidates = [row for row in found if row['name'] == name and row[column] > 0]
        if len(candidates) == 1:
            selected[name] = candidates[0]
    return found, selected


def validate_devices(print_inventory=False):
    found, selected = device_inventory()
    if len(selected) != len(EXPECTED):
        print(json.dumps({'devices': found, 'required_names_found': len(selected)}, ensure_ascii=True), flush=True)
        raise LocalFailure(110)
    valid = True
    for name, (key, count) in EXPECTED.items():
        check(selected[name]['allowed'], 111)
        mix = endpoint_mix(name, 'Capture' if key == 'max_input_channels' else 'Render')
        selected[name]['mix_format'] = mix
        valid = valid and mix.get('error_hresult') == 0 and mix.get('channels') == count and mix.get('rate') == RATE
    if print_inventory or not valid:
        print(json.dumps({'devices': found, 'required_names_found': len(selected)}, ensure_ascii=True), flush=True)
    check(valid, 112)
    return selected


def cpu_identity():
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
        name = winreg.QueryValueEx(key, 'ProcessorNameString')[0].strip()
    return {'name': name, 'logical_processors': os.cpu_count()}


def stats(values):
    values = [value for value in values if value is not None and math.isfinite(value)]
    if not values:
        return {'count': 0, 'median': None, 'min': None, 'max': None, 'spread': None}
    ordered = sorted(float(value) for value in values)
    middle = len(ordered) // 2
    median = ordered[middle] if len(ordered) % 2 else (ordered[middle - 1] + ordered[middle]) / 2
    return {'count': len(ordered), 'median': median, 'min': ordered[0], 'max': ordered[-1], 'spread': ordered[-1] - ordered[0]}


class Local:
    def __init__(self, args):
        self.args, self.root = args, Path(args.work).resolve()
        check(not any((parent / '.git').exists() for parent in [self.root, *self.root.parents]), 103)
        self.product = Path(args.product).resolve() if args.product else None
        approved = Path(os.environ['LOCALAPPDATA']) / 'EqualizerAPO-XT-x64-avx2/current'
        self.approved_product = approved.resolve()
        if self.product is not None:
            check(self.product == approved.resolve(), 190)
        self.inputs = Path(args.inputs).resolve() if args.inputs else None
        live = Path(os.environ['LOCALAPPDATA']) / 'EqualizerAPO-XT/config'
        for protected in [self.product, approved.parent.resolve(), self.inputs, live.resolve()]:
            if protected is not None:
                check(not self.root.is_relative_to(protected) and not protected.is_relative_to(self.root), 103)
        self.root.mkdir(parents=True, exist_ok=True)
        self.state_path = inside(self.root / 'state.json', self.root)
        inside(self.root / 'results', self.root)
        self.data = json.loads(self.state_path.read_text()) if self.state_path.exists() else {'schema': 1, 'steps': {}, 'rows': [], 'errors': []}
        check(self.data.get('schema') == 1, 105)
        self.data['cpu'] = cpu_identity()
        self.step_dir = inside(self.root / 'runs' / (args.command + '-' + uuid.uuid4().hex[:12]), self.root)
        check(not any((parent / '.git').exists() for parent in [self.step_dir, *self.step_dir.parents]), 103)
        self.step_dir.mkdir(parents=True)
        self.allowed = []
        if self.product:
            self.allowed.append(self.product / 'Benchmark.exe')
        self.managed = []

    def save(self):
        atomic_json(self.state_path, self.data)
        atomic_json(self.root / 'results/results.json', self.data)

    def add(self, row):
        self.data['rows'].append(row)
        self.save()
        print(json.dumps({'rows_completed': len(self.data['rows']), 'last_cpu_s': row.get('cpu_s'),
                          'last_clean': int(row['clean']) if 'clean' in row else None}), flush=True)

    def run(self, label, command, timeout=600, cwd=None, allowed=(0,), **unused):
        with Child(command, self.step_dir, label, timeout, self.allowed) as child:
            result = child.wait()
        # Shared Benchmark parser reads these paths; never print their contents.
        shutil.copyfile(child.stdout_path, self.step_dir / (label + '.stdout.log'))
        shutil.copyfile(child.stderr_path, self.step_dir / (label + '.stderr.log'))
        if result['exit_code'] not in allowed:
            self.data['last_child_exit'] = result['exit_code']
            self.save()
            raise LocalFailure(124)
        return result

    def start(self, label, command, timeout):
        child = Child(command, self.step_dir, label, timeout, self.allowed)
        self.managed.append(child)
        return child

    def close(self):
        for child in reversed(self.managed):
            child.close()
        remaining = sum(child.process is not None and child.process.poll() is None for child in self.managed)
        open_jobs = sum(child.job is not None for child in self.managed)
        self.data['cleanup'] = {'owned_root_processes_remaining': remaining, 'owned_job_handles_open': open_jobs}
        self.managed.clear()
        check(remaining == 0 and open_jobs == 0, 125)

    def signature(self):
        return {'blocksize': self.args.blocksize, 'latency_s': self.args.latency, 'warmup_s': self.args.warmup,
                'alignment': self.args.alignment, 'rate': RATE, 'music_sha256': self.data.get('prepared', {}).get('music_sha256')}

    def swap_state(self, removed):
        marker = Path(self.args.swap_marker).resolve() if self.args.swap_marker else self.root / 'backup/config.txt.orig.swap.json'
        inside(marker, self.root)
        value = json.loads(marker.read_text(encoding='utf-8-sig')) if marker.exists() else {'removed': 0}
        if removed:
            check(value.get('removed') == 1 and value.get('backup_sha256') == CONFIG_SHA and
                  re.fullmatch(r'[0-9a-f]{64}', value.get('temporary_sha256', '')) is not None and
                  re.fullmatch(r'[0-9a-f]{32}', value.get('transaction', '')) is not None, 170)
        else:
            check(value.get('removed') != 1 and (not marker.exists() or value.get('equal') == 1), 171)
        # Deliberately do not open or modify the live config from this program.
        return value

    def prepared(self):
        value = self.data.get('prepared', {})
        check(value.get('complete') == 1 and Path(value['product']).resolve() == self.approved_product, 105)
        check(digest(self.approved_product / 'Benchmark.exe') == value['benchmark_sha256'], 130)
        for key in ['music', 'feed', 'xt480']:
            check(inside(value[key], self.root).is_file(), 105)
        check(digest(value['music']) == value['music_sha256'] and digest(value['feed']) == value['feed_sha256'], 130)
        check(digest(value['brir']) == value['brir_sha256'] and digest(value['xt480']) == value['xt480_sha256'], 130)
        return value

    def baseline_ok(self):
        check(self.args.alignment == 'timestamps', 142)
        self.prepared()
        base = self.data.get('baseline', {})
        check(base.get('stable') == 1 and base.get('signature') == self.signature(), 140)
        return base['lag']['median']

    def camilla_exe(self):
        prepared = self.prepared()
        executable = inside(prepared['camilla'], self.root)
        check(executable.is_file() and digest(executable) == prepared['camilla_exe_sha256'], 130)
        self.allowed.append(executable)
        return executable

    def prepare(self):
        np, sd, sf, signal, analyse, Bench = modules()
        check(self.inputs is not None and self.product is not None, 105)
        check(45 <= self.args.seconds <= 90, 151)
        if self.args.omniphony_dir:
            try:
                import netCDF4  # dependency preflight before lengthy references
            except ModuleNotFoundError:
                raise LocalFailure(104) from None
            self.omni_paths()
        # A new preparation invalidates old baseline/wire results, but preserves
        # the previous session in an archive under Work.
        if self.data.get('prepared'):
            atomic_json(self.root / 'archives' / (uuid.uuid4().hex + '.json'), self.data)
        self.data = {'schema': 1, 'cpu': cpu_identity(), 'steps': {}, 'rows': [], 'errors': [],
                     'offline': {'phase_a': [], 'comparisons': [], 'steps': {}}}
        owner = self
        class Shared(Bench):
            def save(self):
                owner.save()
            def run(self, label, command, timeout=600, cwd=None, allowed=(0,), **kwargs):
                return owner.run(label, command, timeout, allowed=allowed)
        shared = object.__new__(Shared)
        shared.args = argparse.Namespace(workspace=str(self.root))
        shared.work, shared.inputs, shared.summary = self.step_dir, self.inputs, self.root / 'results'
        shared.summary.mkdir(parents=True, exist_ok=True)
        shared.current, shared.xt, shared.data = self.product, self.product / 'Benchmark.exe', self.data['offline']
        check(shared.xt.is_file(), 105)
        shared.prepare()
        music, _ = sf.read(shared.music, always_2d=True, dtype='float64')
        music = music[:int(self.args.seconds * RATE)]
        check(np.max(np.abs(music)) > 0, 150)
        music *= PEAK / float(np.max(np.abs(music)))
        fade = min(RATE // 20, len(music) // 2)
        music[:fade] *= np.linspace(0, 1, fade)[:, None]
        music[-fade:] *= np.linspace(1, 0, fade)[:, None]
        sf.write(shared.music, music, RATE, subtype='FLOAT')
        padded = np.zeros((len(music), 12), dtype=np.float32)
        padded[:, :2] = music
        sf.write(shared.padded, padded, RATE, subtype='FLOAT')
        shared.duration = len(music) / RATE
        shared.data['audio']['seconds'] = shared.duration
        shared.data['audio']['played_peak_limit'] = PEAK
        # Validate level before any future audible command. A chain that boosts
        # above the cap is rejected rather than silently played louder.
        shared.phase_a2()
        check(shared.data['feed']['latency_samples'] > 0, 160)
        feed, _ = sf.read(shared.feed, always_2d=True)
        check(float(np.max(np.abs(feed))) <= PEAK + 1 / 32768, 150)
        shared.translate()
        repetitions = self.args.repeats or 3
        references = {}
        for frames in [480, 128]:
            for fanout in [None, 0, 7]:
                mode = 'adaptive' if fanout is None else str(fanout)
                for rep in range(1, repetitions + 1):
                    label = f'XT-{frames}-{mode}-{rep}'
                    output = self.step_dir / (label + '.wav')
                    row = shared.xt_run(label, frames, shared.full_config, shared.music, output, fanout)
                    check(row['output_peak'] <= PEAK + 1 / 32768, 150)
                    row.update({'step': 'L0', 'program': 'XT', 'frames': frames, 'threads': mode, 'repeat': rep})
                    self.add(row)
                    if fanout is None and rep == 1:
                        references[frames] = output
        comparison = analyse(references[128], references[480])
        check(comparison['lag_samples'] == 0 and comparison['residual_power_ratio'] < 10 ** (-45 / 10), 160)
        self.data['offline']['xt_batch_comparison'] = comparison
        directory = self.step_dir / 'download-camilla'
        directory.mkdir()
        archive = directory / 'camilla.zip'
        request = urllib.request.Request(CAMILLA_URL, headers={'User-Agent': 'latency-benchmark'})
        with urllib.request.urlopen(request, timeout=60) as response, archive.open('wb') as output:
            deadline = time.monotonic() + 180
            while block := response.read(1024 * 1024):
                check(time.monotonic() < deadline, 122)
                output.write(block)
        check(digest(archive) == CAMILLA_SHA, 130)
        extracted = directory / 'unpacked'
        extracted.mkdir()
        with zipfile.ZipFile(archive) as bundle:
            for name in bundle.namelist():
                inside(extracted / name, extracted)
            bundle.extractall(extracted)
        executables = list(extracted.rglob('camilladsp.exe'))
        check(len(executables) == 1, 105)
        shared.camilla = executables[0]
        self.allowed.append(shared.camilla)
        for frames in [1024, 512, 256, 128, 64]:
            duration = 30 if frames == 64 else shared.duration
            feed_path = shared.feed
            reference = references[480]
            if frames == 64:
                feed_path = self.step_dir / 'feed-30.wav'
                sf.write(feed_path, feed[:30 * RATE], RATE, subtype='FLOAT')
                reference = self.step_dir / 'XT-reference-30.wav'
                samples, _ = sf.read(references[480], frames=30 * RATE, always_2d=True)
                sf.write(reference, samples, RATE, subtype='FLOAT')
            for threaded in [False, True]:
                for rep in range(1, (1 if frames == 64 else repetitions) + 1):
                    label = f'Camilla-{frames}-{int(threaded)}-{rep}'
                    raw = self.step_dir / (label + '.raw')
                    config = shared.camilla_config(frames, threaded, raw)
                    config['devices']['capture']['filename'] = str(feed_path)
                    path = self.step_dir / (label + '.yml')
                    atomic_json(path, config)  # JSON is a YAML subset
                    self.run(label + '-check', [shared.camilla, '-c', path], 60)
                    port = free_port()
                    child = self.start(label, [shared.camilla, '-p', str(port), '-a', '127.0.0.1', '-o', self.step_dir / (label + '.program.log'), path], 900)
                    poller = LoadPoller(port)
                    try:
                        row = child.wait()
                        check(row['exit_code'] == 0, 124)
                    finally:
                        load = poller.finish()
                        child.close()
                    samples = np.fromfile(raw, dtype='<f8')
                    check(len(samples) > RATE * 2 * 20 and len(samples) % 2 == 0 and np.isfinite(samples).all(), 141)
                    wave = self.step_dir / (label + '.wav')
                    sf.write(wave, samples.reshape(-1, 2), RATE, subtype='FLOAT')
                    quality = analyse(wave, reference)
                    check(quality['lag_samples'] == 0 and quality['residual_power_ratio'] < 10 ** (-45 / 10), 160)
                    check(float(np.max(np.abs(samples))) <= PEAK + 1 / 32768, 150)
                    row.update({'step': 'L0', 'program': 'CamillaDSP', 'frames': frames, 'threads': int(threaded), 'repeat': rep,
                                'seconds': duration, 'realtime_factor': duration / row['wall_s'], 'processing_load': load, 'comparison': quality})
                    self.add(row)
        prepared = {'complete': 1, 'seconds': shared.duration, 'music': str(shared.music), 'feed': str(shared.feed),
                    'xt480': str(references[480]), 'music_sha256': digest(shared.music), 'feed_sha256': digest(shared.feed),
                    'camilla': str(shared.camilla), 'camilla_exe_sha256': digest(shared.camilla), 'camilla_archive_sha256': CAMILLA_SHA,
                    'brir': str(shared.brir), 'brir_sha256': digest(shared.brir), 'xt480_sha256': digest(references[480]),
                    'conv': shared.conv, 'final_matrix': shared.final_matrix,
                    'virtual': shared.virtual, 'peak': PEAK, 'product': str(self.product),
                    'benchmark_sha256': digest(shared.xt)}
        self.data['prepared'] = prepared
        self.save()
        if self.args.omniphony_dir:
            self.prepare_omni(prepared, repetitions)
        self.data['steps']['prepare'] = {'complete': 1}
        self.save()

    def omni_paths(self):
        check(self.args.omniphony_dir and self.args.bridge_dir, 105)
        executable = Path(self.args.omniphony_dir).resolve() / 'orender.exe'
        bridge = Path(self.args.bridge_dir).resolve() / 'reference_bridge.dll'
        check(executable.is_file() and bridge.is_file(), 105)
        self.allowed.append(executable)
        self.run('omni-version', [executable, '--version'], 30)
        version = (self.step_dir / 'omni-version.stdout.log').read_text(errors='replace')
        check(re.search(r'\b0\.7\.0\b', version) is not None, 105)
        self.data.setdefault('versions', {})['Omniphony'] = '0.7.0'
        return executable, bridge

    def brir_logged(self, label):
        text = ''.join((self.step_dir / (label + '.' + stream + '.log')).read_text(errors='replace')
                       for stream in ['stdout', 'stderr'])
        return re.search(r'BRIR: .*?11 emitters.*?1 orientations', text) is not None

    def prepare_omni(self, prepared, repetitions):
        np, sd, sf, signal, analyse, Bench = modules()
        try:
            from netCDF4 import Dataset
        except ModuleNotFoundError:
            raise LocalFailure(104) from None
        executable, bridge = self.omni_paths()
        brir, rate = sf.read(prepared['brir'], always_2d=True)
        check(rate == RATE and brir.shape[1] == 22, 112)
        slots = [0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11]
        azimuths, elevations = [30, -30, 0, 150, -150, 90, -90, 45, -45, 135, -135], [0] * 7 + [45] * 4
        responses = np.empty((11, 2, len(brir)))
        for emitter, slot in enumerate(slots):
            for ear in range(2):
                rows = [i for i, item in enumerate(prepared['conv']) if item['feed'][slot] and prepared['final_matrix'][ear][i]]
                check(len(rows) == 1, 160)
                i = rows[0]
                responses[emitter, ear] = brir[:, prepared['conv'][i]['index']] * prepared['conv'][i]['feed'][slot] * prepared['final_matrix'][ear][i]
        sofa = self.step_dir / 'room.sofa'
        with Dataset(sofa, 'w', format='NETCDF4') as dataset:
            for name, size in [('M', 11), ('R', 2), ('N', len(brir)), ('C', 3), ('I', 1), ('E', 1)]:
                dataset.createDimension(name, size)
            for key, value in {'Conventions': 'SOFA', 'Version': '1.0', 'SOFAConventions': 'SimpleFreeFieldHRIR',
                               'SOFAConventionsVersion': '1.0', 'DataType': 'FIR', 'RoomType': 'reverberant'}.items():
                dataset.setncattr(key, value)
            def variable(name, dimensions, values, coordinates=None):
                value = dataset.createVariable(name, 'f8', dimensions)
                value[:] = values
                if coordinates:
                    value.Type, value.Units = coordinates
                return value
            variable('Data.IR', ('M', 'R', 'N'), responses)
            variable('Data.SamplingRate', ('I',), [RATE]).Units = 'hertz'
            variable('Data.Delay', ('I', 'R'), [[0, 0]])
            variable('SourcePosition', ('M', 'C'), np.column_stack([azimuths, elevations, np.full(11, 2)]), ('spherical', 'degree, degree, metre'))
            variable('ListenerPosition', ('I', 'C'), [[0, 0, 0]], ('cartesian', 'metre'))
            variable('ListenerView', ('I', 'C'), [[1, 0, 0]], ('cartesian', 'metre'))
            variable('ListenerUp', ('I', 'C'), [[0, 0, 1]], ('cartesian', 'metre'))
            variable('ReceiverPosition', ('R', 'C', 'I'), np.array([[0, 0.09, 0], [0, -0.09, 0]])[:, :, None], ('cartesian', 'metre'))
            variable('EmitterPosition', ('E', 'C', 'I'), np.zeros((1, 3, 1)), ('cartesian', 'metre'))
        feed, _ = sf.read(prepared['feed'], always_2d=True, dtype='float32')
        payload = feed.astype('<f4').tobytes()
        fmt = struct.pack('<HHIIHHHHI', 0xfffe, 12, RATE, RATE * 48, 48, 32, 22, 32, 0x2d63f) + bytes.fromhex('0300000000001000800000aa00389b71')
        wave = self.step_dir / 'omni-feed.wav'
        wave.write_bytes(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(payload)) + b'WAVEfmt ' +
                         struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(payload)) + payload)
        names = ['FL', 'FR', 'C', 'BL', 'BR', 'SL', 'SR', 'TFL', 'TFR', 'TBL', 'TBR']
        config = {'render': {'current_layout': {'speakers': [{'name': name, 'azimuth': -az, 'elevation': el, 'distance': 2,
                    'spatialize': True, 'gain_db': 0, 'delay_ms': 0} for name, az, el in zip(names, azimuths, elevations)]},
                    'binaural': {'output_mode': 'binaural', 'hrir_source': 'brir', 'brir_sofa_path': str(sofa),
                                 'brir_max_length_s': 0, 'brir_tail_floor_db': 200}}}
        path = self.step_dir / 'omni.yml'
        atomic_json(path, config)
        self.run('omni-bridge-check', [executable, '--config', path, 'check-bridges', '--bridge-path', bridge], 60)
        gain_db = 0.0
        for safety_try in range(3):
            raw = self.step_dir / 'omni-safety.raw'
            command = [executable, '--config', path, '--loglevel', 'info', 'render', wave, '--bridge-path', bridge,
                       '--master-gain', str(gain_db), '--output-backend', 'file', '--output-file', raw, '--output-file-format', 'raw-f32', '--no-osc']
            self.run('omni-safety-' + str(safety_try), command, 600)
            check(self.brir_logged('omni-safety-' + str(safety_try)), 160)
            samples = np.fromfile(raw, dtype='<f4')
            check(samples.size > RATE * 40 and np.isfinite(samples).all(), 141)
            peak = float(np.max(np.abs(samples)))
            if peak <= PEAK:
                break
            gain_db += 20 * math.log10(PEAK * 0.99 / peak)
        check(peak <= PEAK, 150)
        prepared['omni'] = {'config': str(path), 'feed': str(wave), 'feed_sha256': digest(wave),
                            'sofa': str(sofa), 'sofa_sha256': digest(sofa),
                            'safety_gain_db': gain_db, 'config_object': config,
                            'orender_sha256': digest(executable), 'bridge_sha256': digest(bridge)}
        for rep in range(1, repetitions + 1):
            raw = self.step_dir / f'omni-offline-{rep}.raw'
            command = [executable, '--config', path, '--loglevel', 'info', 'render', wave, '--bridge-path', bridge,
                       '--master-gain', str(gain_db), '--output-backend', 'file', '--output-file', raw, '--output-file-format', 'raw-f32', '--no-osc']
            row = self.run(f'omni-offline-{rep}', command, 600)
            check(self.brir_logged(f'omni-offline-{rep}'), 160)
            samples = np.fromfile(raw, dtype='<f4')
            check(np.isfinite(samples).all() and float(np.max(np.abs(samples))) <= PEAK + 1e-5, 150)
            output = self.step_dir / f'omni-offline-{rep}.wav'
            sf.write(output, samples.reshape(-1, 2), RATE, subtype='FLOAT')
            row.update({'step': 'L0', 'program': 'Omniphony', 'repeat': rep, 'seconds': prepared['seconds'],
                        'realtime_factor': prepared['seconds'] / row['wall_s'], 'safety_gain_db': gain_db,
                        'comparison': analyse(output, prepared['xt480'])})
            self.add(row)
            if rep == 1:
                prepared['omni']['reference'] = str(output)
                prepared['omni']['reference_sha256'] = digest(output)
        self.save()

    def duplex(self, label, render_name, samples, reference, record_channels=(0, 1), child=None):
        np, sd, sf, signal, analyse, Bench = modules()
        devices = validate_devices()
        check(render_name in (RENDER_8, RENDER_16) and not any(word in render_name.lower() for word in FORBIDDEN), 111)
        out_channels = EXPECTED[render_name][1]
        check(samples.ndim == 2 and samples.shape[1] == out_channels and np.isfinite(samples).all(), 112)
        check(float(np.max(np.abs(samples))) <= PEAK + 1 / 32768, 150)
        warmup, preroll, tail = int(self.args.warmup * RATE), RATE // 2, RATE * 2
        playback = np.zeros((warmup + preroll + len(samples) + tail, out_channels), dtype='float32')
        playback[warmup + preroll:warmup + preroll + len(samples)] = samples
        fade = min(RATE // 20, len(samples))
        end_of_music = warmup + preroll + len(samples)
        playback[end_of_music - fade:end_of_music] *= np.linspace(1, 0, fade, dtype='float32')[:, None]
        recording = np.zeros((len(playback), 16), dtype='float32')
        position, failure = 0, []
        finished = threading.Event()
        flags = {key: 0 for key in ['input_overflow', 'input_underflow', 'output_overflow', 'output_underflow']}
        startup_flags = dict(flags)
        origins = []
        def callback(indata, outdata, frames, timing, status):
            nonlocal position
            count = min(frames, len(playback) - position)
            recording[position:position + count] = indata[:count]
            outdata[:count] = playback[position:position + count]
            outdata[count:] = 0
            active = warmup + preroll + RATE * 3 <= position < warmup + preroll + len(samples) - RATE
            for key in flags:
                if getattr(status, key):
                    (flags if active else startup_flags)[key] += 1
            if active:
                adc, dac = float(timing.inputBufferAdcTime), float(timing.outputBufferDacTime)
                if math.isfinite(adc) and math.isfinite(dac) and adc > 0 and dac > 0:
                    origins.append((dac - adc) * RATE)
            position += count
            if position >= len(playback):
                raise sd.CallbackStop
        import ctypes
        timer = ctypes.WinDLL('winmm')
        timer_active = timer.timeBeginPeriod(1) == 0
        initial_cpu, latest_cpu = {}, {}
        cpu_start, cpu_end = None, None
        program_start, program_end = None, None
        start = time.perf_counter()
        try:
            settings = sd.WasapiSettings(exclusive=False, auto_convert=False)
            with sd.Stream(device=(devices[CAPTURE]['index'], devices[render_name]['index']), channels=(16, out_channels),
                           samplerate=RATE, dtype='float32', blocksize=self.args.blocksize, latency=self.args.latency,
                           extra_settings=(settings, settings), callback=callback, finished_callback=finished.set):
                while not finished.wait(0.1):
                    if child is not None:
                        check(child.poll() is None, 124)
                        if position >= warmup + preroll and program_start is None:
                            program_start = (time.perf_counter(), cpu_time(int(child.process._handle)))
                        if position >= end_of_music and program_end is None:
                            program_end = (time.perf_counter(), cpu_time(int(child.process._handle)))
                    if position >= warmup + preroll:
                        values = audiodg_times()
                        if values:
                            if cpu_start is None:
                                initial_cpu = values
                                cpu_start = time.perf_counter()
                            latest_cpu.update(values)
                            cpu_end = time.perf_counter()
                    check(time.perf_counter() - start < len(playback) / RATE + 30, 141)
            check(position == len(playback), 141)
        finally:
            if timer_active:
                timer.timeEndPeriod(1)
        check(np.isfinite(recording).all(), 141)
        output = self.step_dir / (label + '.wav')
        sf.write(output, recording[warmup:, list(record_channels)], RATE, subtype='FLOAT')
        quality = analyse(output, reference) if reference is not None else None
        origin = stats(origins)
        timestamps_ok = origin['count'] > 10 and origin['spread'] <= RATE * 0.001
        raw_lag = quality['lag_samples'] - preroll if quality else None
        corrected = raw_lag - origin['median'] if quality and timestamps_ok else None
        lag = corrected if self.args.alignment == 'timestamps' else raw_lag
        row = {'recording': str(output), 'render_name': render_name, 'capture_name': CAPTURE, 'rate': RATE,
               'render_channels': out_channels, 'capture_channels': 16, 'record_channels': list(record_channels),
               'raw_lag_without_preroll_samples': raw_lag, 'timestamp_origin_samples': origin,
               'alignment': self.args.alignment, 'lag_samples': lag, 'quality': quality, 'callback_flags': flags,
               'startup_and_tail_flags': startup_flags, 'warmup_s': self.args.warmup,
               'audiodg_cpu_s': sum(max(0, value - initial_cpu.get(pid, 0)) for pid, value in latest_cpu.items()) if latest_cpu else None,
               'audiodg_cpu_window_s': cpu_end - cpu_start if cpu_start is not None else None,
               'audiodg_processes_observed': len(latest_cpu), 'timer_period_ms': 1 if timer_active else None}
        if program_start and program_end:
            row['program_cpu_window_s'] = program_end[0] - program_start[0]
            row['program_cpu_s'] = program_end[1] - program_start[1] if program_start[1] is not None and program_end[1] is not None and program_end[1] >= program_start[1] else None
        if quality:
            row['clean'] = not any(flags.values()) and not quality['recording_dropouts'] and quality['residual_power_ratio'] < 0.001
        row['timestamps_valid'] = int(timestamps_ok)
        atomic_json(self.step_dir / (label + '.json'), row)
        self.data['last_audio_diagnostic'] = row
        self.save()
        check(self.args.alignment == 'raw' or timestamps_ok, 142)
        return row, recording[warmup:], preroll

    def baseline_or_xt(self, xt=False):
        np, sd, sf, signal, analyse, Bench = modules()
        self.swap_state(False)
        prepared = self.prepared()
        seconds = self.args.seconds
        check(45 <= seconds <= prepared['seconds'], 151)
        l0 = self.baseline_ok() if xt else None
        if not xt:
            self.data['baseline'] = {'stable': 0, 'lag': stats([]), 'signature': self.signature()}
            self.data['wire'] = {'complete': 0}
            self.data.pop('camilla_selection', None)
            self.save()
        music, _ = sf.read(prepared['music'], frames=int(seconds * RATE), always_2d=True, dtype='float32')
        samples = np.zeros((len(music), 16 if xt else 8), dtype='float32')
        samples[:, :2] = music
        reference = prepared['xt480'] if xt else prepared['music']
        reference = self.reference_excerpt(reference, seconds, 'reference')
        rows = []
        for rep in range(1, (self.args.repeats or 5) + 1):
            row, _, _ = self.duplex(('xt' if xt else 'baseline') + '-' + str(rep), RENDER_16 if xt else RENDER_8, samples, reference)
            row.update({'step': 'L2' if xt else 'L1', 'program': 'XT' if xt else 'Cable baseline', 'repeat': rep,
                        'added_samples': row['lag_samples'] - l0 if xt else row['lag_samples'],
                        'added_ms': ((row['lag_samples'] - l0) if xt else row['lag_samples']) * 1000 / RATE})
            rows.append(row)
            self.add(row)
        if not xt:
            lag = stats([row['lag_samples'] for row in rows])
            stable = all(row['clean'] for row in rows) and lag['spread'] <= 48
            self.data['baseline'] = {'stable': int(stable), 'lag': lag, 'signature': self.signature()}
            self.save()
            print(json.dumps({'baseline_stable': int(stable), 'baseline_spread_samples': lag['spread'], 'baseline_median_samples': lag['median']}), flush=True)
            check(stable, 140)
        self.data['steps']['xt' if xt else 'baseline'] = {'complete': 1, 'lag': stats([row['lag_samples'] for row in rows]),
            'added_samples': stats([row['added_samples'] for row in rows]), 'added_ms': stats([row['added_ms'] for row in rows]),
            'clean_added_samples': stats([row['added_samples'] for row in rows if row['clean']]),
            'clean_runs': sum(int(row['clean']) for row in rows)}
        self.save()

    def reference_excerpt(self, source, seconds, label):
        np, sd, sf, signal, analyse, Bench = modules()
        samples, rate = sf.read(source, frames=int(seconds * RATE), always_2d=True)
        check(rate == RATE and len(samples) >= int(seconds * RATE), 151)
        path = self.step_dir / (label + '.wav')
        sf.write(path, samples, RATE, subtype='FLOAT')
        return path

    def realtime_config(self, frames, target, threaded, adjust=True, wire=False):
        np, sd, sf, signal, analyse, Bench = modules()
        prepared = self.prepared()
        dummy = object.__new__(Bench)
        dummy.conv, dummy.final_matrix = prepared['conv'], prepared['final_matrix']
        dummy.feed, dummy.brir = Path(prepared['feed']), Path(prepared['brir'])
        config = dummy.camilla_config(frames, threaded, self.step_dir / 'unused.raw')
        devices = config['devices']
        devices.update({'enable_rate_adjust': adjust, 'target_level': target, 'adjust_period': 1.0,
                        'silence_threshold': 0.0, 'silence_timeout': 0.0})
        if adjust:
            devices['resampler'] = {'type': 'AsyncSinc', 'profile': 'Balanced'}
        devices['capture'] = {'type': 'Wasapi', 'channels': 16, 'device': CAPTURE, 'format': 'F32', 'exclusive': False, 'loopback': False}
        devices['playback'] = {'type': 'Wasapi', 'channels': 8, 'device': RENDER_8, 'format': 'F32', 'exclusive': False}
        config['mixers']['fanout']['channels']['in'] = 16
        for output in config['mixers']['fanout']['mapping']:
            for source in output['sources']:
                source['channel'] += 2
                check(2 <= source['channel'] <= 13, 160)
        pad = np.zeros((8, 2))
        pad[:2, :2] = np.eye(2)
        config['mixers']['output-pad'] = Bench.mixer(pad.tolist())
        config['pipeline'].append({'type': 'Mixer', 'name': 'output-pad'})
        if wire:
            matrix = np.zeros((8, 16))
            matrix[0, 2], matrix[1, 3] = 1, 1
            config['mixers'] = {'wire': Bench.mixer(matrix.tolist())}
            config['filters'] = {}
            config['pipeline'] = [{'type': 'Mixer', 'name': 'wire'}]
        return config

    def camilla_start(self, label, config, seconds):
        executable = self.camilla_exe()
        path = self.step_dir / (label + '.yml')
        atomic_json(path, config)
        self.run(label + '-check', [executable, '-c', path], 60)
        port = free_port()
        log = self.step_dir / (label + '.program.log')
        child = self.start(label, [executable, '-a', '127.0.0.1', '-p', str(port), '-o', log, path], seconds + self.args.warmup + 60)
        poller = LoadPoller(port)
        for _ in range(20):
            check(child.poll() is None, 124)
            time.sleep(0.1)
        return child, poller, log

    @staticmethod
    def camilla_warnings(path):
        text = path.read_text(errors='replace') if path.exists() else ''
        patterns = ['Playback interrupted', 'buffer underrun', 'samples were dropped', 'dropping chunk', 'pausing stream', 'above 100%']
        return {pattern: text.lower().count(pattern.lower()) for pattern in patterns}

    def wire(self):
        np, sd, sf, signal, analyse, Bench = modules()
        swap = self.swap_state(True)
        self.baseline_ok()
        self.data['wire'] = {'complete': 0, 'swap_transaction': swap['transaction']}
        self.save()
        check(5 <= self.args.seconds <= 30, 151)
        count = int(self.args.seconds * RATE)
        rng = np.random.default_rng(714)
        signal_data = signal.sosfilt(signal.butter(4, 4000, fs=RATE, output='sos'), rng.standard_normal((count, 12)), axis=0)
        signal_data *= 0.05 / np.max(np.abs(signal_data))
        ramp = np.minimum(np.arange(count) / (RATE * 0.1), 1)
        ramp *= np.minimum(np.arange(count)[::-1] / (RATE * 0.1), 1)
        signal_data *= ramp[:, None]
        samples = np.zeros((count, 16), dtype='float32')
        samples[:, 2:14] = signal_data
        direct, recording, pre = self.duplex('wire-direct', RENDER_16, samples, None, record_channels=tuple(range(16)))
        expected = signal_data[RATE:count - RATE]
        search = recording[pre:pre + count + RATE, 2]
        correlation = signal.correlate(search, expected[:, 0], method='fft', mode='valid')
        offset = int(np.argmax(np.abs(correlation)))
        actual = recording[pre + offset:pre + offset + len(expected), 2:14]
        gains = np.sum(actual * expected, axis=0) / np.maximum(np.sum(actual * actual, axis=0), 1e-30)
        ratios = np.sum((actual * gains - expected) ** 2, axis=0) / np.sum(expected ** 2, axis=0)
        silent_stereo = float(np.max(np.abs(recording[pre:pre + count, :2])))
        self.data['wire_direct_diagnostic'] = {'gains_to_reference': gains.tolist(), 'residual_power_ratios': ratios.tolist(), 'stereo_peak': silent_stereo}
        self.save()
        check(np.all(ratios < 1e-4) and np.all(np.abs(gains - 1) < 0.01) and silent_stereo < 1e-5, 160)
        config = self.realtime_config(512, 512, False, False, wire=True)
        child, poller, log = self.camilla_start('wire-camilla', config, self.args.seconds)
        try:
            routed, recording, pre = self.duplex('wire-routed', RENDER_16, samples, None, child=child, record_channels=tuple(range(16)))
        finally:
            load = poller.finish()
            child.close()
        load['interpretation'] = 'Realtime wire-check processing load, including setup and warmup.'
        search = recording[pre:pre + count + RATE, 0]
        offset = int(np.argmax(np.abs(signal.correlate(search, expected[:, 0], method='fft', mode='valid'))))
        stereo = recording[pre + offset:pre + offset + len(expected), :2]
        gain = float(np.sum(stereo * expected[:, :2]) / max(np.sum(stereo ** 2), 1e-30))
        ratio = float(np.sum((stereo * gain - expected[:, :2]) ** 2) / np.sum(expected[:, :2] ** 2))
        tail = float(np.sqrt(np.mean(recording[-RATE:, :2] ** 2)))
        feed_search = recording[pre:pre + count + RATE, 2]
        feed_offset = int(np.argmax(np.abs(signal.correlate(feed_search, expected[:, 0], mode='valid', method='fft'))))
        routed_feed = recording[pre + feed_offset:pre + feed_offset + len(expected), 2:14]
        feed_gains = np.sum(routed_feed * expected, axis=0) / np.maximum(np.sum(routed_feed ** 2, axis=0), 1e-30)
        feed_ratios = np.sum((routed_feed * feed_gains - expected) ** 2, axis=0) / np.sum(expected ** 2, axis=0)
        warnings = self.camilla_warnings(log)
        ok = ratio < 1e-4 and tail < 1e-5 and np.all(feed_ratios < 1e-4) and np.all(np.abs(feed_gains - 1) < 0.01) and not any(warnings.values())
        self.data['wire'] = {'complete': int(ok), 'signature': self.signature(), 'swap_transaction': swap['transaction'], 'direct_channel_residual_power_ratios': ratios.tolist(),
                             'direct_stereo_peak': silent_stereo, 'routed_residual_power_ratio': ratio,
                             'routed_feed_gains': feed_gains.tolist(), 'routed_feed_residual_power_ratios': feed_ratios.tolist(),
                             'feedback_tail_rms': tail, 'program_warning_counts': warnings, 'processing_load': load,
                             'read_channels': list(range(2, 14)), 'write_channels': [0, 1]}
        self.save()
        check(ok, 160)

    def camilla_round(self, settings, label):
        np, sd, sf, signal, analyse, Bench = modules()
        l0 = self.baseline_ok()
        prepared = self.prepared()
        check(45 <= self.args.seconds <= prepared['seconds'], 151)
        feed, _ = sf.read(prepared['feed'], frames=int(self.args.seconds * RATE), always_2d=True, dtype='float32')
        check(digest(prepared['feed']) == prepared['feed_sha256'], 130)
        samples = np.zeros((len(feed), 16), dtype='float32')
        samples[:, 2:14] = feed
        reference = self.reference_excerpt(prepared['xt480'], self.args.seconds, label + '-reference')
        config = self.realtime_config(settings['frames'], settings['target'], settings['multithreaded'], settings['rate_adjust'])
        child, poller, log = self.camilla_start(label, config, self.args.seconds)
        try:
            row, _, _ = self.duplex(label, RENDER_16, samples, reference, child=child)
        finally:
            load = poller.finish()
            child.close()
        warnings = self.camilla_warnings(log)
        load['interpretation'] = 'Realtime GetProcessingLoad over this process lifetime, including setup, warmup and tail; not aggregate process CPU percent.'
        row.update({'step': 'L3', 'program': 'CamillaDSP plus one more cable pass', 'trial': label, 'settings': settings,
                    'added_samples': row['lag_samples'] - l0, 'added_ms': (row['lag_samples'] - l0) * 1000 / RATE,
                    'cpu_s': row.get('program_cpu_s'), 'process_lifetime_cpu_s': child.metrics['cpu_s'],
                    'process_lifetime_wall_s': child.metrics['wall_s'], 'processing_load': load, 'program_warning_counts': warnings})
        row['clean'] = row['clean'] and not any(warnings.values())
        self.add(row)
        return row

    def camilla_trial(self, settings, label):
        try:
            return self.camilla_round(settings, label)
        except LocalFailure as exc:
            if exc.code not in (122, 124, 141, 142):
                raise
            self.close()
            row = {'step': 'L3', 'program': 'CamillaDSP plus one more cable pass', 'trial': label,
                   'settings': settings, 'clean': False, 'failure_code': exc.code,
                   'added_samples': None, 'added_ms': None, 'cpu_s': None}
            self.add(row)
            return row

    def camilla(self):
        swap = self.swap_state(True)
        self.baseline_ok()
        check(self.data.get('wire', {}).get('complete') == 1 and self.data['wire']['signature'] == self.signature() and
              self.data['wire'].get('swap_transaction') == swap['transaction'], 160)
        self.data['camilla_selection'] = {'cpu': self.data['cpu'], 'lowest_clean': None, 'repeats': []}
        self.save()
        rows = []
        for frames in [1024, 512, 256, 128, 64]:
            for target in sorted({frames, frames // 2, 64}, reverse=True):
                for threaded in [False, True]:
                    settings = {'frames': frames, 'target': target, 'multithreaded': threaded, 'rate_adjust': True}
                    rows.append(self.camilla_trial(settings, f'camilla-{frames}-{target}-{int(threaded)}-on'))
        clean = sorted((row for row in rows if row['clean']), key=lambda row: row['added_samples'])
        off_settings = []
        measured = [row for row in rows if 'quality' in row]
        off_bases = clean[:3] or sorted(measured, key=lambda row: (
            sum(row['program_warning_counts'].values()) + sum(row['callback_flags'].values()) + row['quality']['zero_gap_events'],
            row['quality']['residual_power_ratio'], row['added_samples']))[:3]
        if not off_bases:
            off_bases = [{'settings': {'frames': frames, 'target': frames, 'multithreaded': False, 'rate_adjust': True}}
                         for frames in [1024, 512, 256]]
        for row in off_bases:
            for threaded in [False, True]:
                settings = dict(row['settings'], multithreaded=threaded, rate_adjust=False)
                if settings not in off_settings:
                    off_settings.append(settings)
        for index, settings in enumerate(off_settings):
            rows.append(self.camilla_trial(settings, f'camilla-off-{index}'))
        clean = sorted((row for row in rows if row['clean']), key=lambda row: row['added_samples'])
        self.data['camilla_selection'] = {'cpu': self.data['cpu'], 'lowest_clean': clean[0]['settings'] if clean else None, 'repeats': []}
        for index, row in enumerate(clean[:3]):
            repetitions = [self.camilla_trial(row['settings'], f'camilla-best-{index}-{rep}') for rep in range(self.args.repeats or 3)]
            self.data['camilla_selection']['repeats'].append({'settings': row['settings'], 'all_clean': all(item['clean'] for item in repetitions),
                'added_samples': stats([item['added_samples'] for item in repetitions]), 'added_ms': stats([item['added_ms'] for item in repetitions])})
            self.save()
        validated = [item for item in self.data['camilla_selection']['repeats'] if item['all_clean']]
        winner = min(validated, key=lambda item: item['added_samples']['median']) if validated else None
        self.data['camilla_selection']['lowest_clean'] = winner['settings'] if winner else None
        self.data['steps']['camilla'] = {'complete': 1, 'confirmed_clean_settings': len(validated)}
        self.save()
        print(json.dumps({'camilla_complete': 1, 'confirmed_clean_settings': len(validated), 'lowest_clean_found': int(winner is not None)}), flush=True)

    def omniphony(self):
        np, sd, sf, signal, analyse, Bench = modules()
        self.swap_state(True)
        self.baseline_ok()
        guard = asio_registry_counts()
        self.data['omniphony_asio_guard'] = guard
        self.save()
        print(json.dumps(guard), flush=True)
        check(guard['registered_count'] == 0 and guard['unreadable_roots'] == 0, 181)
        prepared = self.prepared()
        check('omni' in prepared, 105)
        executable, bridge = self.omni_paths()
        check(digest(executable) == prepared['omni']['orender_sha256'] and digest(bridge) == prepared['omni']['bridge_sha256'], 130)
        for key in ['feed', 'sofa', 'reference']:
            check(digest(inside(prepared['omni'][key], self.root)) == prepared['omni'][key + '_sha256'], 130)
        devices = validate_devices()
        check(45 <= self.args.seconds <= prepared['seconds'], 151)
        # Never derive a latency from process start or recording alignment here.
        # Only the renderer's own millisecond telemetry is a reported latency.
        rows = []
        repeats = self.args.repeats or 1
        trials = [target for target in [None, 100, 50, 20, 10, 5, 2, 1] for _ in range(repeats)]
        for trial, target in enumerate(trials):
            label = 'omni-realtime-' + ('unset' if target is None else str(target)) + '-' + str(trial)
            config = copy.deepcopy(prepared['omni']['config_object'])
            if target is not None:
                config['render']['latency_target'] = target
            path = self.step_dir / (label + '.yml')
            atomic_json(path, config)
            # Use a valid extensible prefix containing just this trial's audio.
            feed, _ = sf.read(prepared['omni']['feed'], frames=int(self.args.seconds * RATE), always_2d=True, dtype='float32')
            fade = min(RATE // 20, len(feed))
            feed[-fade:] *= np.linspace(1, 0, fade, dtype='float32')[:, None]
            feed = np.concatenate([feed, np.zeros((2 * RATE, 12), dtype='float32')])
            wave = self.step_dir / (label + '-feed.wav')
            sf.write(wave, feed, RATE, format='WAVEX', subtype='FLOAT')
            payload = bytearray(wave.read_bytes())
            fmt_position = payload.index(b'fmt ') + 8
            check(struct.unpack_from('<H', payload, fmt_position)[0] == 0xfffe, 112)
            struct.pack_into('<I', payload, fmt_position + 20, 0x2d63f)
            wave.write_bytes(payload)
            control_reservation = reserve_control_port()
            receive_port = control_reservation.getsockname()[1]
            metrics = OscMetrics()
            command = [executable, '--config', path, '--loglevel', 'info', '--log-format', 'json', 'render', wave,
                '--bridge-path', bridge, '--master-gain', str(prepared['omni']['safety_gain_db']), '--output-backend', 'asio',
                '--output-device', RENDER_8, '--osc', '--osc-metering', '--osc-host', '127.0.0.1', '--osc-port', str(metrics.port), '--osc-rx-port', str(receive_port)]
            recorded = np.zeros((int((self.args.seconds + 20) * RATE), 16), dtype='float32')
            position = 0
            flags = {'input_overflow': 0, 'input_underflow': 0}
            def callback(indata, frames, timing, status):
                nonlocal position
                count = min(frames, len(recorded) - position)
                recorded[position:position + count] = indata[:count]
                position += count
                for key in flags:
                    if getattr(status, key):
                        flags[key] += 1
                if position >= len(recorded):
                    raise sd.CallbackStop
            child = None
            try:
                with timer_resolution() as timer_active, sd.InputStream(device=devices[CAPTURE]['index'], channels=16, samplerate=RATE, dtype='float32', blocksize=self.args.blocksize,
                    latency=self.args.latency, extra_settings=sd.WasapiSettings(exclusive=False, auto_convert=False), callback=callback):
                    child = self.start(label, command, self.args.seconds + 30)
                    process = child.wait()
                    check(process['exit_code'] == 0, 124)
                    time.sleep(2)
            finally:
                reported = metrics.close()
                if child:
                    child.close()
                control_reservation.close()
            output = self.step_dir / (label + '.wav')
            sf.write(output, recorded[:position, :2], RATE, subtype='FLOAT')
            reference = self.reference_excerpt(prepared['omni']['reference'], self.args.seconds, label + '-reference')
            # File startup is not an input clock. Find the waveform anywhere
            # in the recording solely for the signal/dropout check, then omit
            # every alignment offset from the public latency result.
            expected, _ = sf.read(reference, always_2d=True)
            actual = recorded[:position, :2].astype('float64')
            template = expected[10 * RATE:20 * RATE, 0]
            coarse = int(np.argmax(np.abs(signal.correlate(actual[:, 0], template, mode='valid', method='fft')))) - 10 * RATE
            shift = max(0, coarse - RATE // 2)
            aligned = self.step_dir / (label + '-aligned.wav')
            sf.write(aligned, actual[shift:], RATE, subtype='FLOAT')
            quality = analyse(aligned, reference)
            # Alignment offsets are internal to the waveform check, not latency.
            for key in ['lag_samples', 'lag_ms', 'segments', 'segment_lag_spread_samples']:
                quality.pop(key, None)
            logs = child.stdout_path.read_text(errors='replace') + child.stderr_path.read_text(errors='replace')
            brir_loaded = re.search(r'BRIR: .*?11 emitters.*?1 orientations', logs) is not None
            window_start, window_end = child.started + 3, child.started + process['wall_s'] - 2
            whole = {key: [value for _, value in values] for key, values in reported.items()}
            steady = {key: [value for timestamp, value in values if window_start <= timestamp <= window_end] for key, values in reported.items()}
            counters = {key: max(whole.get(key, [])) if whole.get(key) else None for key in ['frames_lost', 'frames_filled', 'realigns', 'underruns']}
            deltas = {key: (steady[key][-1] - steady[key][0]) if len(steady.get(key, [])) >= 2 else None for key in counters}
            for key in counters:
                values = re.findall(r'\b' + key + r'\b["\s]*[:=]["\s]*(\d+)', logs)
                if values:
                    counters[key] = max(int(value) for value in values)
            warnings = {word: sum(word in line.lower() and bool(re.search(r'warn|error', line, re.I)) for line in logs.splitlines())
                        for word in ['underrun', 'realign', 'buffer overrun', 'clock mismatch']}
            instant = steady.get('instant_latency_ms', [])
            log_latencies = [float(value) for value in re.findall(r'\blatency=(-?\d+(?:\.\d+)?)ms\b', logs)]
            reported_latency = instant or log_latencies
            latency_source = 'OSC instantaneous' if instant else 'log snapshot' if log_latencies else 'not reported for this source'
            accounting_known = all(deltas[key] is not None for key in deltas)
            reference_match = quality['residual_power_ratio'] < 0.001
            clean = brir_loaded and accounting_known and reference_match and not any(flags.values()) and not quality['recording_dropouts'] and not any(warnings.values()) and all(value == 0 for value in deltas.values())
            row = {'step': 'L4', 'program': 'Omniphony', 'latency_target_ms': target, 'repeat': trial % repeats + 1,
                   'reported_latency_ms': stats(reported_latency), 'reported_latency_source': latency_source,
                   'diagnostic_latency_ms_not_an_input_clock': stats(steady.get('latency_ms', [])),
                   'reported_target_ms': stats(steady.get('latency_target_ms', [])),
                   'osc_control_listener_disabled': 1, 'timer_period_ms': 1 if timer_active else None,
                   'telemetry_window_start_s': 3, 'telemetry_window_end_s': process['wall_s'] - 2,
                   'steady_counter_deltas': deltas, 'accounting_known': int(accounting_known),
                   'documented_algorithmic_latency_samples': 127, 'cpu_s': process['cpu_s'], 'wall_s': process['wall_s'],
                   'quality': quality, 'callback_flags': flags, 'program_warning_counts': warnings, 'program_counters': counters,
                   'brir_load_confirmed': int(brir_loaded), 'reference_match': int(reference_match),
                   'clean': clean, 'safety_gain_db': prepared['omni']['safety_gain_db'],
                   'latency_definition': 'Renderer-reported latency only. No startup-inclusive or file-alignment delay is presented as latency.'}
            self.add(row)
            rows.append(row)
        clean = [row for row in rows if row['clean']]
        timed = [row for row in clean if row['reported_latency_ms']['count']]
        requested = [row for row in clean if row['latency_target_ms'] is not None]
        self.data['omni_selection'] = {
            'lowest_clean_reported_latency': min(timed, key=lambda row: row['reported_latency_ms']['median']) if timed else None,
            'lowest_clean_requested_target_ms': min(row['latency_target_ms'] for row in requested) if requested else None}
        self.data['steps']['omniphony'] = {'complete': 1, 'clean_cases': len(clean)}
        self.save()
        print(json.dumps({'omniphony_complete': 1, 'clean_cases': len(clean), 'lowest_clean_found': int(bool(clean)),
                          'reported_latency_cases': len(timed)}), flush=True)

    def report(self):
        lines = ['# Local latency benchmark', '', 'Runtime results only; static validation is not measurement.', '',
                 'CPU: ' + self.data['cpu']['name'], '',
                 'L1 is cable delay, aligned with the selected callback timestamp policy. L2 is lag versus XT offline output minus L1 median; the VST delay is already in the reference.',
                 'L3 is CamillaDSP plus one more pass through the cable, minus L1 median. L4 is the renderer\'s own reported latency; file playback has no externally defined input instant.', '',
                 '## L0 offline', '', '| Program | Frames | Threads | Repeat | Wall s | CPU s | x realtime | XT CPU % | Reported samples |', '|---|---:|---|---:|---:|---:|---:|---:|---:|']
        def number(value):
            if value is None:
                return 'not measured'
            return f'{value:.6g}' if isinstance(value, float) else str(value)
        for row in self.data['rows']:
            if row['step'] == 'L0':
                lines.append('| ' + ' | '.join(number(row.get(key)) for key in ['program', 'frames', 'threads', 'repeat', 'wall_s', 'cpu_s', 'realtime_factor', 'xt_cpu_percent', 'latency_samples']) + ' |')
        lines += ['', '## L0 waveform comparisons', '', '| Program | Frames | Repeat | Lag samples | Gain | Residual dB |', '|---|---:|---:|---:|---:|---:|']
        for row in self.data['rows']:
            if row['step'] == 'L0' and 'comparison' in row:
                quality = row['comparison']
                lines.append('| ' + ' | '.join(number(value) for value in [row['program'], row.get('frames'), row.get('repeat'),
                    quality['lag_samples'], quality['gain'], quality['residual_db']]) + ' |')
        if self.data.get('offline', {}).get('xt_batch_comparison'):
            quality = self.data['offline']['xt_batch_comparison']
            lines += ['', 'XT 128 vs 480 lag/gain/residual: ' + json.dumps({key: quality[key] for key in ['lag_samples', 'gain', 'residual_db']})]
        if self.data.get('offline', {}).get('feed'):
            lines += ['', 'Upmixed per-channel RMS: ' + json.dumps(self.data['offline']['feed']['rms_per_channel'])]
        lines += ['', '## L1-L3 realtime', '', '| Step | Program | Settings/repetition | Added samples | Added ms | Clean | CPU s | Processing load mean % |', '|---|---|---|---:|---:|---|---:|---:|']
        for row in self.data['rows']:
            if row['step'] in ['L1', 'L2', 'L3']:
                lines.append('| ' + ' | '.join(number(value) for value in [row['step'], row['program'], json.dumps(row.get('settings', {'repeat': row.get('repeat')})), row.get('added_samples'), row.get('added_ms'), row['clean'], row.get('cpu_s', row.get('audiodg_cpu_s')), row.get('processing_load', {}).get('mean_percent')]) + ' |')
        if self.data.get('baseline'):
            lines += ['', 'Baseline stable: ' + str(self.data['baseline']['stable']), 'Baseline samples: ' + json.dumps(self.data['baseline']['lag']),
                      'A spread above 48 samples (1 ms), dirty recording, or unstable timestamps blocks subsequent steps.']
        if self.data.get('steps', {}).get('xt'):
            lines += ['', 'XT repetitions: ' + json.dumps(self.data['steps']['xt'])]
        if self.data.get('camilla_selection'):
            lines += ['', 'Lowest clean CamillaDSP setting on ' + self.data['cpu']['name'] + ': ' + json.dumps(self.data['camilla_selection']['lowest_clean']),
                      'Three-setting repetitions: ' + json.dumps(self.data['camilla_selection']['repeats'])]
        lines += ['', '## L4 renderer-reported latency', '', '| Requested ms | Reported target median ms | Reported instantaneous median ms | Reported spread ms | CPU s | Clean |', '|---|---:|---:|---:|---:|---|']
        for row in self.data['rows']:
            if row['step'] == 'L4':
                lines.append('| ' + ' | '.join(number(value) for value in [row['latency_target_ms'] if row['latency_target_ms'] is not None else 'unset', row['reported_target_ms']['median'], row['reported_latency_ms']['median'], row['reported_latency_ms']['spread'], row['cpu_s'], row['clean']]) + ' |')
        if self.data.get('omni_selection'):
            lines += ['', 'Lowest clean requested Omniphony target (not an achieved latency): ' + number(self.data['omni_selection']['lowest_clean_requested_target_ms'])]
        lines += ['', 'Omniphony\'s documented convolution delay is 127 samples; it is not a measurement. A zero-initialized diagnostic metric is not used as an instantaneous latency. Missing source-clock telemetry stays not measured.',
                  'The OSC control receiver is deliberately prevented from binding; only loopback telemetry is consumed. A logged control-port bind failure is expected. Registered ASIO drivers block local Omniphony realtime launch to avoid probing unrelated hardware.',
                  'Rate-adjusted CamillaDSP uses AsyncSinc Balanced and adjust_period=1.0 s; the no-adjust trials omit the resampler.',
                  'Safety gain is recorded separately from comparison gain. Music has 50 ms fades; references use the same prepared music, and realtime tail fades are outside the analysis interval. Child processes run BelowNormal.',
                  'No installer, device-setting/default-device operation or live-config write exists in this Python entry point.',
                  'The raw alignment option is diagnostic only; subsequent steps require a timestamp-aligned baseline. Timestamp origin spread must be at most 48 samples, independently of repeat-to-repeat baseline spread.',
                  'audiodg CPU is the sum of accessible audiodg processes, including unrelated user audio; unavailable counters are null. Its actual observation duration is recorded per trial.',
                  'CamillaDSP warnings and GetProcessingLoad cover its complete process lifetime, including initialization/warmup/tail. Omniphony whole-run counters and steady counter deltas (after 3 s, before the final 2 s) are both retained.',
                  'A clean recording requires no callback faults, no unexpected >1 ms zero gap, no residual spikes, and residual power below -30 dB. Unknown Omniphony accounting cannot establish a clean setting.',
                  'Detailed thresholds, timestamp origin distributions, warnings, counters and failures are retained in results.json. Missing telemetry is null, not zero.']
        for error in self.data.get('errors', []):
            lines.append('Failure ' + str(error['code']) + ': ' + ERRORS.get(error['code'], ERRORS[900]))
        self.save()
        (self.root / 'results/results.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def bridge_workspace(root, source):
    import tomllib
    root, source = Path(root).resolve(), Path(source).resolve()
    inside(source, root)
    check(digest(root / 'omni-source.tar') == SOURCE_SHA, 130)
    output = root / 'omni-bridge-build'
    check(not output.exists(), 105)
    output.mkdir()
    for crate in ['reference_bridge', 'bridge_api']:
        shutil.copytree(source / 'omniphony-renderer' / crate, output / crate)
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
    (output / 'Cargo.toml').write_text(manifest, encoding='ascii')
    chunks = (source / 'omniphony-renderer/Cargo.lock').read_text().split('[[package]]')[1:]
    packages = [tomllib.loads('[[package]]' + chunk)['package'][0] for chunk in chunks]
    wanted, pending = set(), [('reference_bridge', '0.7.0'), ('bridge_api', '0.6.0')]
    while pending:
        name, version = pending.pop()
        candidates = [i for i, item in enumerate(packages) if item['name'] == name and (version is None or item['version'] == version)]
        check(len(candidates) == 1, 105)
        index = candidates[0]
        if index in wanted:
            continue
        wanted.add(index)
        check(not packages[index].get('source', '').startswith('git+'), 130)
        for dependency in packages[index].get('dependencies', []):
            fields = dependency.split()
            pending.append((fields[0], fields[1] if len(fields) > 1 else None))
    (output / 'Cargo.lock').write_text('version = 4\n\n' + ''.join('[[package]]' + chunks[i] for i in sorted(wanted)), encoding='utf-8')


def main():
    class NumericParser(argparse.ArgumentParser):
        def error(self, message):
            print(json.dumps({'ok': 0, 'error_code': 10}), flush=True)
            raise SystemExit(2)
    parser = NumericParser()
    parser.add_argument('command', choices=['devices', 'prepare', 'baseline', 'xt', 'camilla-wire', 'camilla', 'omniphony', 'report', '_bridge-workspace'])
    parser.add_argument('--work', required=True)
    parser.add_argument('--inputs')
    parser.add_argument('--product')
    parser.add_argument('--omniphony-dir')
    parser.add_argument('--bridge-dir')
    parser.add_argument('--source')
    parser.add_argument('--swap-marker')
    parser.add_argument('--repeats', type=int)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--warmup', type=float, default=15)
    parser.add_argument('--blocksize', type=int, default=480)
    parser.add_argument('--latency', type=float, default=0.02)
    parser.add_argument('--alignment', choices=['timestamps', 'raw'], default='timestamps')
    args = parser.parse_args()
    local = None
    try:
        check(os.name == 'nt' and os.environ.get('GITHUB_ACTIONS') != 'true', 100)
        import ctypes
        check(not ctypes.windll.shell32.IsUserAnAdmin(), 101)
        print(json.dumps({'harness_pid': os.getpid()}), flush=True)
        check(args.seconds > 0 and args.warmup >= 0 and args.blocksize >= 0 and args.latency > 0 and (args.repeats is None or 1 <= args.repeats <= 10), 151)
        if args.command == '_bridge-workspace':
            check(args.source is not None, 105)
            bridge_workspace(args.work, args.source)
        else:
            local = Local(args)
            if args.command in ['baseline', 'xt', 'camilla-wire', 'camilla', 'omniphony']:
                local.data['devices'] = validate_devices(print_inventory=True)
                local.save()
            if args.command == 'devices':
                local.data['devices'] = validate_devices(print_inventory=True)
                guard = asio_registry_counts()
                local.data['omniphony_asio_preflight'] = guard
                print(json.dumps({'omniphony_realtime_allowed': int(guard['registered_count'] == 0 and guard['unreadable_roots'] == 0),
                                  'registered_asio_drivers': guard['registered_count'], 'unreadable_asio_roots': guard['unreadable_roots']}), flush=True)
                local.save()
            elif args.command == 'prepare':
                local.prepare()
            elif args.command == 'baseline':
                local.baseline_or_xt(False)
            elif args.command == 'xt':
                local.baseline_or_xt(True)
            elif args.command == 'camilla-wire':
                local.wire()
            elif args.command == 'camilla':
                local.camilla()
            elif args.command == 'omniphony':
                local.omniphony()
            if local:
                local.close()
                local.report()
        print(json.dumps({'ok': 1, 'error_code': 0, 'owned_root_processes_remaining': 0, 'owned_job_handles_open': 0}), flush=True)
        return 0
    except BaseException as exc:
        code = exc.code if isinstance(exc, LocalFailure) else 199 if isinstance(exc, KeyboardInterrupt) else 104 if isinstance(exc, ModuleNotFoundError) else 900
        if local:
            try:
                local.close()
            except BaseException:
                code = 125
            import traceback
            local.data['errors'].append({'code': code, 'exception_type': type(exc).__name__,
                'locations': [{'function': frame.name, 'line': frame.lineno} for frame in traceback.extract_tb(exc.__traceback__)
                              if Path(frame.filename).parent == Path(__file__).parent]})
            local.report()
        print(json.dumps({'ok': 0, 'error_code': code, 'baseline_unstable': int(code == 140)}), flush=True)
        return 1
    finally:
        if local:
            try:
                local.close()
            except BaseException:
                print(json.dumps({'ok': 0, 'error_code': 125, 'cleanup_failed': 1}), flush=True)
                raise SystemExit(1)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except BaseException:
        # A secondary reporting failure must not print arbitrary exception
        # strings or private configuration/audio values through a traceback.
        print(json.dumps({'ok': 0, 'error_code': 900}), flush=True)
        raise SystemExit(1) from None
