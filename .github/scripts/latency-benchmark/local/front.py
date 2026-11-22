"""zAuro standalone on a hidden desktop, wired to the virtual cable through its settings file."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hidden

EXE = r'C:\Program Files\zSonic\zAuro\zAuro Pro 2D-3D Upmixer.exe'
SETTINGS = os.path.join(os.environ['APPDATA'], 'zAuro Pro 2D-3D Upmixer', 'zAuro Pro 2D-3D Upmixer.settings')
HERE = os.path.dirname(os.path.abspath(__file__))


def write_settings(mute, buffer, device_type='Windows Audio', in_chans='1100000000000000', out_chans='11111111111100',
                   out_name='CABLE In 16ch(VB-Audio Virtual Cable)', in_name='CABLE Output(VB-Audio Virtual Cable)'):
    text = open(SETTINGS, encoding='utf-8').read()
    setup = (f'<VALUE name="audioSetup">\n    <DEVICESETUP deviceType="{device_type}" audioOutputDeviceName="{out_name}"\n'
             f'                 audioInputDeviceName="{in_name}" audioDeviceRate="48000.0"\n'
             f'                 audioDeviceBufferSize="{buffer}" audioDeviceInChans="{in_chans}"\n'
             f'                 audioDeviceOutChans="{out_chans}"/>\n  </VALUE>')
    text, n = re.subn(r'<VALUE name="audioSetup"(?: val=""/>|>.*?</VALUE>)', lambda m: setup, text, flags=re.S)
    assert n == 1
    text, n = re.subn(r'<VALUE name="shouldMuteInput" val="\d"/>', f'<VALUE name="shouldMuteInput" val="{int(mute)}"/>', text)
    assert n == 1
    open(SETTINGS, 'w', encoding='utf-8', newline='').write(text)


def read_setup():
    text = open(SETTINGS, encoding='utf-8').read()
    m = re.search(r'<VALUE name="audioSetup".*?(?:/>|</VALUE>)', text, flags=re.S)
    mute = re.search(r'name="shouldMuteInput" val="(\d)"', text)
    return re.sub(r'\s+', ' ', m.group(0)) if m else None, mute.group(1) if mute else None


def sessions_of(pid):
    """Names of the endpoints (render and capture) on which the process holds an audio session."""
    from pycaw.pycaw import AudioUtilities, IAudioSessionManager2, IAudioSessionControl2
    from comtypes import CLSCTX_ALL
    names = []
    for dev in AudioUtilities.GetAllDevices():
        try:
            if dev.state.value != 1 and getattr(dev.state, 'name', '') != 'Active':
                continue
            mgr = dev._dev.Activate(IAudioSessionManager2._iid_, CLSCTX_ALL, None).QueryInterface(IAudioSessionManager2)
            enum = mgr.GetSessionEnumerator()
            for i in range(enum.GetCount()):
                ctl = enum.GetSession(i).QueryInterface(IAudioSessionControl2)
                if ctl.GetProcessId() == pid:
                    names.append(dev.FriendlyName)
        except Exception:
            continue
    return names


class Front:
    def __init__(self):
        self.h = hidden.Hidden(EXE)
        self.pid = self.h.pid

    def close(self):
        out = os.path.join(HERE, 'closewin.out')
        h2 = hidden.Hidden(sys.executable, args=['-I', os.path.join(HERE, 'closewin.py'), str(self.pid), out, 'close'])
        for _ in range(40):
            time.sleep(0.25)
            if not self.h.alive():
                break
        h2.stop()
        graceful = not self.h.alive()
        self.h.stop()
        return graceful


if __name__ == '__main__':
    mute, buffer = int(sys.argv[1]), int(sys.argv[2])
    dtype = sys.argv[3] if len(sys.argv) > 3 else 'Windows Audio'
    write_settings(mute, buffer, dtype)
    f = Front()
    time.sleep(12)
    print('sessions', sessions_of(f.pid))
    print('closed gracefully', f.close())
    print('saved', read_setup())
