"""Scene definitions shared by the XT and CamillaDSP measurements.
s71  : zAuro 7.1 output, 7 speakers x 2 ears = 14 convolutions
s714 : zAuro 7.1.4 output, 11 speakers x 2 ears = 22 convolutions (the user's own setting)
s916 : zAuro 7.1.4 output plus four channels synthesised from their neighbours
       (wide L/R, top middle L/R), 15 speakers x 2 ears = 30 convolutions.
       Their BRIR tracks are the mean of the neighbouring tracks, in a temporary file."""
import os, re
import numpy as np
import soundfile as sf

ORDER_714 = ['FL', 'FR', 'FC', 'LFE', 'BL', 'BR', 'SL', 'SR', 'TFL', 'TFR', 'TBL', 'TBR']
SYNTH = {'WL': ('FL', 'SL'), 'WR': ('FR', 'SR'), 'TML': ('TFL', 'TBL'), 'TMR': ('TFR', 'TBR')}


class Scene:
    def __init__(self, name, assets, workdir):
        self.name = name
        text = open(os.path.join(assets, 'config-714.txt'), 'rb').read().decode('utf-8')
        lines = text.splitlines()
        self.vst_line = [l for l in lines if l.strip().lower().startswith('vstplugin:')][0]
        conv = [l for l in lines if l.startswith('MultiConvolution:')][0]
        self.track = dict((k, int(v)) for k, v in re.findall(r'\b([VE][A-Z]+)=(\d+)', conv))
        self.brir_src = os.path.join(assets, 'brir22.wav')
        if name == 's71':
            self.plugin_out = ORDER_714[:8]
            self.layout = '7.1'
        else:
            self.plugin_out = list(ORDER_714)
            self.layout = '7.1.4'
        self.synth = dict(SYNTH) if name == 's916' else {}
        self.speakers = [s for s in self.plugin_out if s != 'LFE'] + list(self.synth)
        self.brir_name = f'_bench_{name}_brir.wav'
        self.workdir = workdir

    # --- BRIR file for this scene -------------------------------------------------
    def write_brir(self, folder):
        data, rate = sf.read(self.brir_src, dtype='float64', always_2d=True)
        cols, self.tracks = [], {}
        for s in self.speakers:
            for prefix in ('V', 'E'):
                if s in self.synth:
                    a, b = self.synth[s]
                    col = 0.5 * (data[:, self.track[prefix + a]] + data[:, self.track[prefix + b]])
                else:
                    col = data[:, self.track[prefix + s]]
                self.tracks[prefix + s] = len(cols)
                cols.append(col)
        path = os.path.join(folder, self.brir_name)
        sf.write(path, np.stack(cols, axis=1), rate, subtype='FLOAT')
        return path

    def tracks_only(self):
        self.tracks = {}
        n = 0
        for s in self.speakers:
            for prefix in ('V', 'E'):
                self.tracks[prefix + s] = n
                n += 1

    # --- XT configuration lines ---------------------------------------------------
    def plugin_lines(self, keep_chunk=True):
        v = ' '.join('V' + s for s in self.plugin_out)
        line = self.vst_line
        line = re.sub(r'Output \S+ OutputChannels \S+', 'Output ' + self.layout + ' OutputChannels ' +
                      ','.join('V' + s for s in self.plugin_out), line)
        if not keep_chunk:
            line = re.sub(r'\s*ChunkData\s+"[^"]*"', '', line)
        return ['NewChannel: ' + v, line]

    def synth_lines(self):
        if not self.synth:
            return []
        names = ' '.join('V' + s for s in self.synth)
        mix = ' '.join(f'V{s}=0.5*V{a}+0.5*V{b}' for s, (a, b) in self.synth.items())
        return ['NewChannel: ' + names, 'Copy: ' + mix]

    def chain_lines(self, keep_chunk=True):
        sp = self.speakers
        out = self.plugin_lines(keep_chunk) + self.synth_lines()
        out += ['Copy: VFC=VFC+VLFE',
                'Copy: ' + ' '.join(f'E{s}=V{s}' for s in sp),
                'MultiConvolution: ' + ' '.join(f'{p}{s}={self.tracks[p + s]}' for s in sp for p in ('V', 'E')) + ' ' + self.brir_name,
                'Copy: L=' + '+'.join('V' + s for s in sp) + ' R=' + '+'.join('E' + s for s in sp)]
        return out

    # --- CamillaDSP: sources of each speaker among the plugin's output channels ---
    def sources(self, speaker):
        """[(plugin channel index, linear gain)] feeding this speaker's convolver."""
        if speaker in self.synth:
            a, b = self.synth[speaker]
            return [(self.plugin_out.index(a), 0.5), (self.plugin_out.index(b), 0.5)]
        idx = [(self.plugin_out.index(speaker), 1.0)]
        if speaker == 'FC':
            idx.append((self.plugin_out.index('LFE'), 1.0))
        return idx
