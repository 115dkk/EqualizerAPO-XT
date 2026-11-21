"""Explicit WASAPI shared duplex playback/recording; runner only.

Audio stays in Work. JSON contains only device/settings and numeric diagnostics.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import threading
import time

import numpy as np
import psutil
import scipy.signal as signal
import sounddevice as sd
import soundfile as sf

RATE = 48000


def device_index(name, direction):
    key = 'max_input_channels' if direction == 'capture' else 'max_output_channels'
    found = []
    for index, device in enumerate(sd.query_devices()):
        api = sd.query_hostapis(device['hostapi'])['name']
        if api == 'Windows WASAPI' and device[key] > 0 and (
                device['name'] == name or device['name'].startswith(name + ' (')):
            found.append(index)
    if len(found) != 1:
        raise RuntimeError('Explicit endpoint name did not resolve uniquely in WASAPI.')
    return found[0]


def audio_cpu():
    values = {}
    for process in psutil.process_iter(['name']):
        if (process.info['name'] or '').lower() == 'audiodg.exe':
            try:
                cpu = process.cpu_times()
                values[process.pid] = cpu.user + cpu.system
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
    return values


def record_play(play, recording, render_name, capture_name, channels=2):
    audio, rate = sf.read(play, always_2d=True, dtype='float32')
    if rate != RATE or audio.shape[1] > channels:
        raise RuntimeError('Playback file does not match the requested stream format.')
    render = device_index(render_name, 'render')
    capture = device_index(capture_name, 'capture')
    # The same sample-counted preroll in every round is included in L0. It is
    # not a sleep between separately started capture and playback streams.
    # Hosted VB-CABLE changed its buffering during the first 5-10 seconds
    # in run 44962864585; all measured 15-87 s segments then matched near
    # -80 dB. Warm the *same open stream* for 15 s before the fixed preroll.
    # Discard only this declared warmup, never a discovered bad music segment.
    warmup = RATE * 15
    preroll = RATE // 2
    padded = np.zeros((warmup + preroll + len(audio) + RATE * 2, channels), dtype='float32')
    padded[warmup + preroll:warmup + preroll + len(audio), :audio.shape[1]] = audio
    captured = np.zeros_like(padded)
    completed = threading.Event()
    position = 0
    flags = {name: 0 for name in ['input_underflow', 'input_overflow', 'output_underflow', 'output_overflow']}
    startup_flags = dict(flags)
    callbacks = 0

    def callback(indata, outdata, frames, timing, status):
        nonlocal position, callbacks
        count = min(frames, len(padded) - position)
        if count > 0:
            captured[position:position + count] = indata[:count]
            outdata[:count] = padded[position:position + count]
        outdata[count:] = 0
        counters = flags if warmup + RATE * 3 <= position < warmup + preroll + len(audio) - RATE else startup_flags
        for name in flags:
            if getattr(status, name):
                counters[name] += 1
        callbacks += 1
        position += count
        if position >= len(padded):
            raise sd.CallbackStop

    initial_cpu = audio_cpu()
    observed_cpu = dict(initial_cpu)
    started = time.perf_counter()
    settings = sd.WasapiSettings(exclusive=False, auto_convert=False)
    with sd.Stream(device=(capture, render), samplerate=RATE, channels=(channels, channels),
                   dtype='float32', blocksize=480, latency=0.02,
                   extra_settings=(settings, settings), callback=callback,
                   finished_callback=completed.set) as stream:
        latencies = list(stream.latency)
        while not completed.wait(timeout=0.5):
            observed_cpu.update(audio_cpu())
            if time.perf_counter() - started > len(padded) / RATE + 40:
                raise TimeoutError('Duplex stream did not finish within its deadline.')
    observed_cpu.update(audio_cpu())
    if position != len(padded):
        raise RuntimeError('Duplex stream ended before all frames were recorded.')
    sf.write(recording, captured[warmup:], RATE, subtype='FLOAT')
    return {'render_name': render_name, 'capture_name': capture_name, 'render_index': render,
            'capture_index': capture, 'sample_rate': RATE, 'channels': channels,
            'shared_mode': True, 'blocksize': 480, 'requested_latency_s': 0.02,
            'reported_latency_s': latencies, 'preroll_frames': preroll,
            'recorded_frames': position - warmup, 'discarded_warmup_frames': warmup,
            'audiodg_cpu_window_s': len(padded) / RATE,
            'callbacks': callbacks, 'callback_flags': flags,
            'startup_and_tail_flags': startup_flags,
            'audiodg_cpu_s': sum(max(0, value - initial_cpu.get(pid, 0)) for pid, value in observed_cpu.items()) if observed_cpu else None,
            'audiodg_processes_observed': len(observed_cpu)}


def analyse(recording, reference):
    y, ry = sf.read(recording, always_2d=True, dtype='float64')
    x, rx = sf.read(reference, always_2d=True, dtype='float64')
    if ry != RATE or rx != RATE or x.shape[1] != y.shape[1]:
        raise RuntimeError('Recording and reference formats differ.')
    start, count = RATE * 10, RATE * 30
    # A fixed 30-second excerpt, including the known sample-counted preroll in
    # the tested range. Search +/- two seconds, as in offline comparisons.
    a, b = y[start:start + count, 0], x[start:start + count, 0]
    correlation = signal.correlate(a - a.mean(), b - b.mean(), method='fft')
    lags = signal.correlation_lags(len(a), len(b))
    use = np.abs(lags) <= RATE * 2
    lag = int(lags[use][np.argmax(np.abs(correlation[use]))])
    segments = []
    for seconds in range(5, int(len(x) / RATE) - 3, 10):
        offset, length, margin = seconds * RATE, 2 * RATE, RATE // 4
        left = offset + lag - margin
        if left < 0 or left + length + 2 * margin > len(y):
            continue
        reference_piece = x[offset:offset + length]
        recording_search = y[left:left + length + 2 * margin]
        local_corr = signal.correlate(recording_search[:, 0], reference_piece[:, 0], mode='valid', method='fft')
        peak = int(np.argmax(np.abs(local_corr)))
        piece = recording_search[peak:peak + length]
        energy = float(np.sum(piece ** 2))
        if energy <= 0:
            continue
        local_gain = float(np.sum(piece * reference_piece) / energy)
        local_ratio = float(np.sum((local_gain * piece - reference_piece) ** 2) / np.sum(reference_piece ** 2))
        segments.append({'start_s': seconds, 'lag_samples': lag - margin + peak,
                         'gain': local_gain, 'residual_db': float(10 * np.log10(local_ratio)) if local_ratio > 0 else None})
    begin = RATE * 3
    end = min(len(x) - RATE, len(y) - lag - RATE)
    expected = x[begin:end]
    actual = y[begin + lag:end + lag]
    if len(actual) != len(expected) or not np.isfinite(actual).all() or np.sum(actual ** 2) <= 0:
        raise RuntimeError('Recording has no valid aligned signal.')
    gain = float(np.sum(actual * expected) / np.sum(actual * actual))
    residual = gain * actual - expected
    ratio = float(np.sum(residual ** 2) / np.sum(expected ** 2))
    residual_db = float(10 * np.log10(ratio)) if ratio > 0 else None
    block = 480
    nblocks = len(actual) // block
    def block_power(values):
        return np.mean(values[:nblocks * block].reshape(nblocks, block, -1) ** 2, axis=(1, 2))
    ref_power = block_power(expected)
    errors = block_power(residual) / np.maximum(ref_power, 1e-16)
    active = ref_power > 1e-9
    median = float(np.median(errors[active])) if active.any() else 0.0
    threshold = max(0.01, median * 10)
    spikes = active & (errors > threshold)
    transitions = np.diff(np.r_[False, spikes, False].astype(np.int8))
    spike_events = int(np.sum(transitions == 1))
    # Gaps must last over 1 ms and have reference signal in the same frames.
    silent = (np.max(np.abs(actual), axis=1) <= 1e-9) & (np.max(np.abs(expected), axis=1) > 1e-5)
    edges = np.diff(np.r_[False, silent, False].astype(np.int8))
    lengths = np.flatnonzero(edges == -1) - np.flatnonzero(edges == 1)
    gaps = int(np.sum(lengths > RATE // 1000))
    return {'lag_samples': lag, 'lag_ms': lag * 1000 / RATE, 'gain': gain,
            'residual_db': residual_db, 'residual_power_ratio': ratio,
            'residual_spike_events': spike_events, 'residual_spike_blocks': int(np.sum(spikes)),
            'residual_spike_threshold_power_ratio': threshold, 'zero_gap_events': gaps,
            'longest_zero_gap_frames': int(np.max(lengths)) if len(lengths) else 0,
            'analysis_start_s': begin / RATE, 'analysis_end_s': end / RATE,
            'segments': segments,
            'segment_lag_spread_samples': max(row['lag_samples'] for row in segments) - min(row['lag_samples'] for row in segments) if segments else None,
            'recording_dropouts': bool(spike_events or gaps)}


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Runner only.')
    parser = argparse.ArgumentParser()
    for name in ['play', 'record', 'reference', 'render-name', 'capture-name', 'json']:
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--channels', type=int, default=2)
    args = parser.parse_args()
    path = Path(args.json)
    timer = ctypes.WinDLL('winmm')
    timer_started = timer.timeBeginPeriod(1) == 0
    try:
        data = record_play(args.play, args.record, args.render_name, args.capture_name, args.channels)
        data['timer_period_ms'] = 1 if timer_started else None
        data['analysis'] = analyse(args.record, args.reference)
        data['clean'] = not data['analysis']['recording_dropouts'] and not any(data['callback_flags'].values())
        data['clean_definition'] = 'No active-music WASAPI callback under/overflow, no >1ms unexpected zero gap, no 10ms residual block above max(-20dB, 10x median residual power ratio). Startup/tail flags are reported separately.'
        path.write_text(json.dumps(data, indent=2, allow_nan=False), encoding='ascii')
        return 0
    except Exception as exc:
        # PortAudio's errors can name endpoint settings, but do not export
        # arbitrary exception strings or stack traces from audio processing.
        data = {'error_type': type(exc).__name__, 'portaudio_error_code': None}
        if isinstance(exc, sd.PortAudioError):
            data['portaudio_error_code'] = exc.args[1] if len(exc.args) > 1 and isinstance(exc.args[1], int) else None
        path.write_text(json.dumps(data, indent=2), encoding='ascii')
        return 1
    finally:
        if timer_started:
            timer.timeEndPeriod(1)


if __name__ == '__main__':
    raise SystemExit(main())
