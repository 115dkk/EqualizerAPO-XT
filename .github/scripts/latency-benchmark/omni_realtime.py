"""Record an explicitly selected WASAPI capture while orender plays a file.

The source epoch is the process-start request, not a fabricated first-input
clock. Reported delay therefore includes CLI initialization, and is not a pure
steady-state DSP-buffer delay. The recording is never exported from Work.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

import numpy as np
import sounddevice as sd
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
from audio_io import RATE, analyse, device_index
from benchmark import cpu_seconds


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Runner only.')
    parser = argparse.ArgumentParser()
    for key in ['command', 'capture-name', 'record', 'reference', 'clean-reference', 'json', 'log-prefix', 'workspace']:
        parser.add_argument('--' + key, required=True)
    args = parser.parse_args()
    result_path = Path(args.json)
    command = json.loads(Path(args.command).read_text(encoding='utf-8'))
    capture = device_index(args.capture_name, 'capture')
    recorded = np.zeros((RATE * 140, 2), dtype='float32')
    position = 0
    warmup, preroll = RATE * 15, RATE // 2
    last_callback = None
    launch = threading.Event()
    callbacks = {'input_overflow': 0, 'input_underflow': 0}
    startup_flags = dict(callbacks)
    timer = ctypes.WinDLL('winmm')
    timer_started = timer.timeBeginPeriod(1) == 0
    producer = None
    try:
        def callback(indata, frames, timing, status):
            nonlocal position, last_callback
            now = time.perf_counter()
            last_callback = (position, now)
            count = min(frames, len(recorded) - position)
            recorded[position:position + count] = indata[:count]
            counters = callbacks if warmup + RATE * 3 <= position < warmup + preroll + RATE * 89 else startup_flags
            for key in callbacks:
                if getattr(status, key):
                    counters[key] += 1
            position += count
            if position >= warmup + preroll:
                launch.set()
            if position >= len(recorded):
                raise sd.CallbackStop

        settings = sd.WasapiSettings(exclusive=False, auto_convert=False)
        with sd.InputStream(device=capture, channels=2, samplerate=RATE, dtype='float32',
                            blocksize=480, latency=0.02, extra_settings=settings, callback=callback):
            if not launch.wait(timeout=30):
                raise TimeoutError('Capture did not reach the declared source epoch.')
            requested = time.perf_counter()
            frame, callback_time = last_callback
            # Interpolate from the most recent callback delivery. This clocks
            # the *start request*, not when the renderer first decodes audio.
            launch_frame = frame + (requested - callback_time) * RATE
            with Path(args.log_prefix + '.stdout.log').open('wb') as out, Path(args.log_prefix + '.stderr.log').open('wb') as err:
                producer = subprocess.Popen(command, cwd=args.workspace, stdout=out, stderr=err, stdin=subprocess.DEVNULL)
                try:
                    producer.wait(timeout=115)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill', '/PID', str(producer.pid), '/T', '/F'],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
                    producer.wait(timeout=10)
                    raise TimeoutError('Realtime producer exceeded its deadline.') from None
                wall = time.perf_counter() - requested
                cpu = cpu_seconds(producer)
            time.sleep(2)
        sf.write(args.record, recorded[warmup:position], RATE, subtype='FLOAT')
        if producer.returncode != 0:
            result_path.write_text(json.dumps({'producer_exit': producer.returncode, 'producer_wall_s': wall, 'producer_cpu_s': cpu}), encoding='ascii')
            return 1
        data = {'producer_exit': producer.returncode, 'producer_wall_s': wall, 'producer_cpu_s': cpu,
                'capture_name': args.capture_name, 'capture_index': capture, 'shared_mode': True,
                'sample_rate': RATE, 'channels': 2, 'blocksize': 480,
                'discarded_warmup_frames': warmup, 'preroll_frames': preroll,
                'source_epoch_offset_samples': launch_frame - warmup - preroll,
                'source_epoch_definition': 'Process-start request, interpolated from capture callback delivery; includes CLI initialization. Callback-clock resolution is 480 frames (10 ms). Not a steady-state DSP-buffer delay.',
                'timer_period_ms': 1 if timer_started else None,
                'callback_flags': callbacks, 'startup_and_tail_flags': startup_flags}
        data['analysis'] = analyse(args.record, args.reference)
        data['clean_analysis'] = analyse(args.record, args.clean_reference)
        data['recording_clean'] = not data['clean_analysis']['recording_dropouts'] and not any(callbacks.values())
        result_path.write_text(json.dumps(data, indent=2, allow_nan=False), encoding='ascii')
        return 0
    except Exception as exc:
        result_path.write_text(json.dumps({'error_type': type(exc).__name__}), encoding='ascii')
        return 1
    finally:
        if producer is not None and producer.poll() is None:
            subprocess.run(['taskkill', '/PID', str(producer.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
            producer.wait(timeout=10)
        if timer_started:
            timer.timeEndPeriod(1)


if __name__ == '__main__':
    raise SystemExit(main())
