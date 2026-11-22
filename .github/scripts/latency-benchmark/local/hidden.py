"""Start a GUI program on a separate, never-shown desktop so no window reaches the user's desktop."""
import ctypes
from ctypes import wintypes

user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)


class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('lpReserved', wintypes.LPWSTR), ('lpDesktop', wintypes.LPWSTR),
                ('lpTitle', wintypes.LPWSTR), ('dwX', wintypes.DWORD), ('dwY', wintypes.DWORD),
                ('dwXSize', wintypes.DWORD), ('dwYSize', wintypes.DWORD), ('dwXCountChars', wintypes.DWORD),
                ('dwYCountChars', wintypes.DWORD), ('dwFillAttribute', wintypes.DWORD), ('dwFlags', wintypes.DWORD),
                ('wShowWindow', wintypes.WORD), ('cbReserved2', wintypes.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', wintypes.HANDLE), ('hStdOutput', wintypes.HANDLE), ('hStdError', wintypes.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', wintypes.HANDLE), ('hThread', wintypes.HANDLE), ('dwProcessId', wintypes.DWORD),
                ('dwThreadId', wintypes.DWORD)]


user32.CreateDesktopW.restype = wintypes.HANDLE
user32.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
kernel32.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL,
                                    wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(STARTUPINFO),
                                    ctypes.POINTER(PROCESS_INFORMATION)]
GENERIC_ALL = 0x10000000


class Hidden:
    def __init__(self, exe, name='eapo-bench-desktop', args=()):
        self.desktop = user32.CreateDesktopW(name, None, None, 0, GENERIC_ALL, None)
        if not self.desktop:
            raise ctypes.WinError(ctypes.get_last_error())
        si = STARTUPINFO()
        si.cb = ctypes.sizeof(si)
        si.lpDesktop = 'WinSta0\\' + name
        pi = PROCESS_INFORMATION()
        cmd = ctypes.create_unicode_buffer(' '.join('"' + a + '"' for a in (exe,) + tuple(args)))
        if not kernel32.CreateProcessW(exe, cmd, None, None, False, 0, None, None, ctypes.byref(si), ctypes.byref(pi)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.pid = pi.dwProcessId
        self.process = pi.hProcess
        kernel32.CloseHandle(pi.hThread)

    def alive(self):
        return kernel32.WaitForSingleObject(self.process, 0) == 0x102

    def stop(self):
        if self.alive():
            kernel32.TerminateProcess(self.process, 0)
            kernel32.WaitForSingleObject(self.process, 10000)
        kernel32.CloseHandle(self.process)
        user32.CloseDesktop(self.desktop)
