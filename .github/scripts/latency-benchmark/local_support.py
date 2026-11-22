"""Local-only safety, owned child processes, and read-only Windows counters.

Importing this module does not enumerate devices or start any process.
"""
import contextlib
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time


class LocalFailure(Exception):
    def __init__(self, code):
        self.code = int(code)
        super().__init__(str(code))


def check(condition, code):
    if not condition:
        raise LocalFailure(code)


def inside(path, root):
    path = Path(path).resolve()
    check(path.is_relative_to(Path(root).resolve()), 102)
    return path


def sounddevice_module():
    # The installed 0.5.5 loader first searches PATH, and treats mere presence
    # of SD_ENABLE_ASIO as opting into its ASIO DLL. Force the known bundled
    # non-ASIO DLL before importing, so inventory itself cannot probe ASIO.
    import ctypes.util
    import importlib
    import importlib.util
    import sys
    spec = importlib.util.find_spec('_sounddevice_data')
    check(spec is not None and spec.submodule_search_locations, 104)
    library = Path(next(iter(spec.submodule_search_locations))) / 'portaudio-binaries/libportaudio64bit.dll'
    check(library.is_file(), 104)
    if 'sounddevice' in sys.modules:
        module = sys.modules['sounddevice']
        check(Path(module._libname).resolve() == library.resolve(), 111)
        return module
    os.environ.pop('SD_ENABLE_ASIO', None)
    original = ctypes.util.find_library
    def locate(name):
        return str(library) if 'portaudio' in name.lower() else original(name)
    ctypes.util.find_library = locate
    try:
        module = importlib.import_module('sounddevice')
    finally:
        ctypes.util.find_library = original
    check(Path(module._libname).resolve() == library.resolve(), 111)
    return module


def asio_registry_counts():
    # Pinned orender/CPAL enumerates and loads ASIO drivers before choosing a
    # named WASAPI fallback. Do not launch that path when any driver exists.
    import winreg
    names, unreadable = set(), 0
    for hive in [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]:
        try:
            with winreg.OpenKey(hive, r'SOFTWARE\ASIO', 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
                for index in range(winreg.QueryInfoKey(key)[0]):
                    names.add(winreg.EnumKey(key, index))
        except FileNotFoundError:
            pass
        except OSError:
            unreadable += 1
    return {'registered_count': len(names), 'unreadable_roots': unreadable,
            'forbidden_name_count': sum(any(word in name.lower() for word in ['topping', 'realphones', 'steam', 'nvidia']) for name in names)}


def endpoint_mix(name, flow):
    """GetMixFormat only for an already allowlisted, exact friendly name."""
    import uuid
    import winreg
    class Guid(ctypes.Structure):
        _fields_ = [('bytes', ctypes.c_ubyte * 16)]
    def guid(value):
        return Guid.from_buffer_copy(uuid.UUID(value).bytes_le)
    matches = []
    base = r'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio' + '\\' + flow
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, base) as root:
        for index in range(winreg.QueryInfoKey(root)[0]):
            child = winreg.EnumKey(root, index)
            try:
                with winreg.OpenKey(root, child) as endpoint:
                    if winreg.QueryValueEx(endpoint, 'DeviceState')[0] != 1:
                        continue
                    with winreg.OpenKey(endpoint, 'Properties') as props:
                        if winreg.QueryValueEx(props, '{a45c254e-df1c-4efd-8020-67d146a850e0},14')[0] == name:
                            matches.append(child)
            except OSError:
                continue
    if len(matches) != 1:
        return {'error_hresult': -1, 'registry_matches': len(matches)}
    endpoint_id = ('{0.0.0.00000000}.' if flow == 'Render' else '{0.0.1.00000000}.') + matches[0]
    ole = ctypes.WinDLL('ole32', use_last_error=True)
    ole.CoInitializeEx.argtypes = [ctypes.c_void_p, wintypes.DWORD]
    ole.CoInitializeEx.restype = wintypes.LONG
    ole.CoCreateInstance.argtypes = [ctypes.POINTER(Guid), ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Guid), ctypes.POINTER(ctypes.c_void_p)]
    ole.CoCreateInstance.restype = wintypes.LONG
    ole.CoTaskMemFree.argtypes = [ctypes.c_void_p]
    initialized = ole.CoInitializeEx(None, 0)
    pointers, format_pointer = [], ctypes.c_void_p()
    def call(pointer, slot, return_type, argument_types, *arguments):
        table = ctypes.cast(pointer, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        function = ctypes.WINFUNCTYPE(return_type, ctypes.c_void_p, *argument_types)(table[slot])
        return function(pointer, *arguments)
    try:
        enumerator = ctypes.c_void_p()
        result = ole.CoCreateInstance(ctypes.byref(guid('BCDE0395-E52F-467C-8E3D-C4579291692E')), None, 1,
            ctypes.byref(guid('A95664D2-9614-4F35-A746-DE8DB63617E6')), ctypes.byref(enumerator))
        if result < 0:
            return {'error_hresult': result}
        pointers.append(enumerator)
        device = ctypes.c_void_p()
        result = call(enumerator, 5, wintypes.LONG, [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p)], endpoint_id, ctypes.byref(device))
        if result < 0:
            return {'error_hresult': result}
        pointers.append(device)
        client = ctypes.c_void_p()
        result = call(device, 3, wintypes.LONG, [ctypes.POINTER(Guid), wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)],
                      ctypes.byref(guid('1CB9AD4C-DBFA-4C32-B178-C2F568A703B2')), 23, None, ctypes.byref(client))
        if result < 0:
            return {'error_hresult': result}
        pointers.append(client)
        result = call(client, 8, wintypes.LONG, [ctypes.POINTER(ctypes.c_void_p)], ctypes.byref(format_pointer))
        if result < 0:
            return {'error_hresult': result}
        tag, channels, rate, average, alignment, bits, extra = struct.unpack('<HHIIHHH', ctypes.string_at(format_pointer, 18))
        return {'channels': channels, 'rate': rate, 'bits': bits, 'tag': tag, 'error_hresult': 0}
    finally:
        if format_pointer.value:
            ole.CoTaskMemFree(format_pointer)
        for pointer in reversed(pointers):
            call(pointer, 2, wintypes.ULONG, [])
        if initialized >= 0:
            ole.CoUninitialize()


def cpu_time(handle):
    values = [wintypes.FILETIME() for _ in range(4)]
    api = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    api.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    api.restype = wintypes.BOOL
    if not api(handle, *(ctypes.byref(value) for value in values)):
        return None
    return sum((value.dwHighDateTime << 32) + value.dwLowDateTime for value in values[2:]) / 1e7


def audiodg_times():
    class Entry(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('usage', wintypes.DWORD), ('pid', wintypes.DWORD),
                    ('heap', ctypes.c_size_t), ('module', wintypes.DWORD), ('threads', wintypes.DWORD),
                    ('parent', wintypes.DWORD), ('priority', wintypes.LONG), ('flags', wintypes.DWORD),
                    ('exe', wintypes.WCHAR * 260)]
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    api.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = api.CreateToolhelp32Snapshot(2, 0)
    if snapshot in (None, ctypes.c_void_p(-1).value):
        return {}
    found = {}
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        more = api.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            if entry.exe.lower() == 'audiodg.exe':
                handle = api.OpenProcess(0x1000, False, entry.pid)
                if handle:
                    try:
                        value = cpu_time(handle)
                        if value is not None:
                            found[entry.pid] = value
                    finally:
                        api.CloseHandle(handle)
            more = api.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        api.CloseHandle(snapshot)
    return found


class Job:
    """Closing this job terminates only processes assigned by this harness."""
    def __init__(self, process):
        class Basic(ctypes.Structure):
            _fields_ = [('process_time', ctypes.c_int64), ('job_time', ctypes.c_int64),
                        ('flags', wintypes.DWORD), ('min_working', ctypes.c_size_t),
                        ('max_working', ctypes.c_size_t), ('active_limit', wintypes.DWORD),
                        ('affinity', ctypes.c_size_t), ('priority', wintypes.DWORD), ('scheduling', wintypes.DWORD)]
        class Io(ctypes.Structure):
            _fields_ = [(key, ctypes.c_uint64) for key in ['read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes']]
        class Extended(ctypes.Structure):
            _fields_ = [('basic', Basic), ('io', Io), ('process_memory', ctypes.c_size_t),
                        ('job_memory', ctypes.c_size_t), ('peak_process', ctypes.c_size_t), ('peak_job', ctypes.c_size_t)]
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.api.CreateJobObjectW.restype = wintypes.HANDLE
        self.api.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.api.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.api.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.api.CreateJobObjectW(None, None)
        check(self.handle, 120)
        limits = Extended()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        try:
            check(self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)), 120)
            check(self.api.AssignProcessToJobObject(self.handle, int(process._handle)), 120)
        except BaseException:
            self.close()
            raise

    def pids(self):
        buffer = ctypes.create_string_buffer(8 + 1024 * ctypes.sizeof(ctypes.c_size_t))
        check(self.api.QueryInformationJobObject(self.handle, 3, buffer, len(buffer), None), 120)
        count = struct.unpack_from('<I', buffer.raw, 4)[0]
        check(count <= 1024, 120)
        array = (ctypes.c_size_t * count).from_buffer(buffer, 8)
        return set(array)

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def visible_owned_window(pids):
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    found = False
    @callback_type
    def inspect(window, unused):
        nonlocal found
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
        if pid.value in pids and user.IsWindowVisible(window):
            found = True
            return False
        return True
    user.EnumWindows(inspect, 0)
    return found


class Child:
    def __init__(self, command, root, label, timeout, allowed_executables):
        executable = Path(command[0]).resolve()
        check(executable in {Path(path).resolve() for path in allowed_executables}, 121)
        check(executable.name.lower() in ['benchmark.exe', 'camilladsp.exe', 'orender.exe'], 121)
        self.root, self.label, self.timeout = Path(root), label, timeout
        directory = inside(self.root / 'logs', self.root)
        directory.mkdir(exist_ok=True, parents=True)
        self.stdout_path, self.stderr_path = directory / (label + '.stdout.log'), directory / (label + '.stderr.log')
        self.stdout = self.stdout_path.open('wb')
        self.stderr = self.stderr_path.open('wb')
        env = os.environ.copy()
        for key, relative in [('TEMP', 'temp'), ('TMP', 'temp'), ('APPDATA', 'profile/roaming'), ('LOCALAPPDATA', 'profile/local')]:
            destination = self.root / relative
            destination.mkdir(parents=True, exist_ok=True)
            env[key] = str(destination)
        if executable.name.lower() == 'orender.exe':
            # orender migrates legacy settings before parsing --config. Keep
            # even that default path and any sidecars inside this trial.
            program_data = self.root / 'profile/programdata'
            program_data.mkdir(parents=True, exist_ok=True)
            env['ProgramData'] = str(program_data)
            for key in list(env):
                if key.upper().startswith('OMNIPHONY_'):
                    env.pop(key)
            config_directory = self.root / 'profile/omniphony'
            config_directory.mkdir(parents=True, exist_ok=True)
            env['OMNIPHONY_CONFIG_DIR'] = str(config_directory)
        env.pop('RUST_LOG', None)
        env.pop('GH_TOKEN', None)
        self.started = time.perf_counter()
        self.process, self.job, self.metrics = None, None, None
        try:
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.SetErrorMode.argtypes = [wintypes.UINT]
            kernel.SetErrorMode.restype = wintypes.UINT
            previous_mode = kernel.SetErrorMode(0x8003)  # inherited, no loader/GP-fault dialogs
            try:
                self.process = subprocess.Popen([str(value) for value in command], cwd=self.root,
                    stdout=self.stdout, stderr=self.stderr, stdin=subprocess.DEVNULL, env=env,
                    creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            finally:
                kernel.SetErrorMode(previous_mode)
            try:
                self.job = Job(self.process)
            except LocalFailure as exc:
                # A short --version/-c invocation may finish before assignment.
                # Never ignore assignment failure while the process is alive.
                if exc.code != 120 or self.process.poll() is None:
                    raise
        except BaseException:
            self.close()
            raise

    def poll(self):
        if time.perf_counter() - self.started > self.timeout:
            self.close()
            raise LocalFailure(122)
        if self.job and visible_owned_window(self.job.pids()):
            self.close()
            raise LocalFailure(123)
        return self.process.poll()

    def wait(self):
        while self.poll() is None:
            time.sleep(0.05)
        self.metrics = {'wall_s': time.perf_counter() - self.started,
                        'cpu_s': cpu_time(int(self.process._handle)), 'exit_code': self.process.returncode}
        return self.metrics

    def close(self):
        if self.process is not None:
            self.metrics = self.metrics or {'wall_s': time.perf_counter() - self.started,
                'cpu_s': cpu_time(int(self.process._handle)), 'exit_code': self.process.poll()}
        if self.job is not None:
            self.job.close()
            self.job = None
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()  # only the Popen process we own
            self.process.wait(timeout=10)
        self.stdout.close()
        self.stderr.close()

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()


@contextlib.contextmanager
def timer_resolution():
    timer = ctypes.WinDLL('winmm')
    active = timer.timeBeginPeriod(1) == 0
    try:
        yield active
    finally:
        if active:
            timer.timeEndPeriod(1)


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def reserve_control_port():
    # Pinned orender v0.7.0 keeps telemetry running when its control listener
    # cannot bind. Reserve an exclusive loopback port so it cannot expose a
    # wildcard control listener or reclaim another application's port.
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    sock.bind(('127.0.0.1', 0))
    return sock


class OscMetrics:
    """Retain only numeric latency/accounting fields from loopback OSC."""
    KEYS = {'latency_ms', 'latency_target_ms', 'latency_target_configured_ms',
            'frames_lost', 'frames_filled', 'realigns', 'underruns'}
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.values = {}
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    @staticmethod
    def messages(packet):
        if packet.startswith(b'#bundle\0'):
            position = 16
            while position + 4 <= len(packet):
                size = struct.unpack_from('!I', packet, position)[0]
                position += 4
                if size > len(packet) - position:
                    return
                yield from OscMetrics.messages(packet[position:position + size])
                position += size
            return
        def string(offset):
            end = packet.index(0, offset)
            return packet[offset:end].decode('utf-8'), (end + 4) & ~3
        address, position = string(0)
        types, position = string(position)
        arguments = []
        for tag in types.lstrip(','):
            if tag == 's':
                value, position = string(position)
            elif tag in 'ifdh':
                code = {'i': 'i', 'f': 'f', 'd': 'd', 'h': 'q'}[tag]
                value = struct.unpack_from('!' + code, packet, position)[0]
                position += struct.calcsize(code)
            else:
                return
            arguments.append(value)
        yield address, arguments

    def _read(self):
        while not self.stop.is_set():
            try:
                packet, sender = self.socket.recvfrom(65535)
                if sender[0] != '127.0.0.1':
                    continue
                for address, args in self.messages(packet):
                    selected = {}
                    if address.endswith('/state/latency_instant') and args:
                        selected['instant_latency_ms'] = args[0]
                    elif address.endswith('/state/latency_target') and args:
                        selected['latency_target_ms'] = args[0]
                    elif address.endswith('/state/diag_values') and args and isinstance(args[0], str):
                        payload = json.loads(args[0])
                        selected = {key: value for key, value in payload.items() if key in self.KEYS}
                    for key, value in selected.items():
                        if isinstance(value, (int, float)) and math.isfinite(value):
                            self.values.setdefault(key, []).append((time.perf_counter(), float(value)))
            except (OSError, ValueError, IndexError, TypeError, AttributeError, struct.error):
                continue

    def close(self):
        self.stop.set()
        self.thread.join(timeout=1)
        self.socket.close()
        return self.values
