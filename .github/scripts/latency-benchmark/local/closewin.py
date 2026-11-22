"""Runs on the hidden desktop: list a process's top-level windows; optionally ask the visible ones to close."""
import ctypes, sys
from ctypes import wintypes
pid = int(sys.argv[1])
user32 = ctypes.WinDLL('user32')
found = []
@ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def each(hwnd, _):
    p = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
    if p.value == pid:
        buf = ctypes.create_unicode_buffer(200)
        user32.GetWindowTextW(hwnd, buf, 200)
        cls = ctypes.create_unicode_buffer(200)
        user32.GetClassNameW(hwnd, cls, 200)
        r = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        found.append((buf.value, cls.value, bool(user32.IsWindowVisible(hwnd)), r.right - r.left, r.bottom - r.top))
        if len(sys.argv) > 3 and user32.IsWindowVisible(hwnd):
            user32.PostMessageW(hwnd, 0x0010, 0, 0)
    return True
user32.EnumWindows(each, 0)
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(map(repr, found)))
