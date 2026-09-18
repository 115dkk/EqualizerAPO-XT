"""DeviceSelector: the install-target dialog (DeviceSelector --skin-shots: 760x700 per skin), two frames per line."""
from rowlib import *

CSS = '''
  .ds-stack { display: grid; grid-template-columns: 1fr 1fr; }
  .ds-stack > .skin { width: auto; padding: 16px; }
  .dlg { width: 760px; box-sizing: border-box; display: flex; flex-direction: column; gap: 8px; padding: 14px; font-size: 12.5px; }
  .ttl { font-weight: 600; margin-bottom: 4px; }
  .sec { display: flex; align-items: center; gap: 8px; height: 26px; padding: 0 8px; font-weight: 700; }
  .dev { position: relative; display: grid; grid-template-columns: 34px 1fr auto; gap: 4px 10px; align-items: center; padding: 6px 10px; }
  .dev .tg { grid-row: span 2; width: 22px; height: 22px; display: inline-flex; align-items: center; justify-content: center; }
  .dev .nm { font-weight: 700; }
  .dev .nm small { font-weight: 500; color: var(--muted); margin-left: 8px; }
  .dev .st { color: var(--muted); font-size: 11.5px; }
  .dev .st.acc { color: var(--accent); }
  .dev .tags { grid-row: span 2; display: inline-flex; align-items: center; gap: 6px; font-family: var(--mono); font-size: 10px; letter-spacing: 1px; color: var(--muted); }
  .dev .tag { border: 1px solid currentColor; padding: 1px 5px; border-radius: 3px; }
  .dev.off { opacity: .5; }
  .disc { display: flex; align-items: center; gap: 8px; color: var(--muted); padding: 4px 8px; }
  .btns { display: flex; justify-content: flex-end; gap: 8px; }
  .btn { height: 28px; display: inline-flex; align-items: center; justify-content: center; padding: 0 16px; font-weight: 600; color: var(--text); }
  .btn.pri { color: var(--on-accent); background: var(--accent); }
  /* studio: glass channel strips; the install toggle is a lit console push button */
  .skin-studio .dlg { background: var(--bg); border: 1px solid var(--border); border-radius: var(--radius); }
  .skin-studio .sec { color: var(--muted); font-size: 11px; letter-spacing: 1px; text-transform: uppercase; border-bottom: 1px solid color-mix(in srgb, var(--accent) 35%, var(--border)); }
  .skin-studio .dev { background: color-mix(in srgb, var(--card) 88%, transparent); border: 1px solid var(--border); border-top-color: rgba(255,255,255,.10); border-radius: var(--radius); }
  .skin-studio .dev.sel { border-color: var(--accent); box-shadow: 0 0 0 2px color-mix(in srgb, var(--accent) 18%, transparent); }
  .skin-studio .tg { border-radius: 50%; background: var(--sunken); border: 1px solid var(--border); }
  .skin-studio .tg.on { background: color-mix(in srgb, var(--accent) 50%, var(--card)); border-color: var(--accent); box-shadow: 0 0 8px color-mix(in srgb, var(--accent) 60%, transparent); }
  .skin-studio .btn { background: var(--card-hover); border: 1px solid var(--border); border-radius: 6px; }
  .skin-studio .btn.pri { background: var(--accent); }
  /* minimal: a terminal menu; [x]/[ ] toggles, numbered entries, :: status lines */
  .skin-minimal .dlg { background: var(--bg); border: 1px solid var(--border); font-family: var(--mono); }
  .skin-minimal .sec { color: var(--text); letter-spacing: 1px; }
  .skin-minimal .sec::before, .skin-minimal .sec::after { content: "=="; color: var(--muted); }
  .skin-minimal .dev { padding: 4px 8px; }
  .skin-minimal .dev.sel { outline: 1px solid var(--accent); }
  .skin-minimal .tg { font-weight: 700; }
  .skin-minimal .dev .nm { font-weight: 700; }
  .skin-minimal .dev .st::before { content: ":: "; }
  .skin-minimal .btn { border: 1px solid var(--border); text-transform: uppercase; letter-spacing: 1px; font-weight: 700; }
  .skin-minimal .btn.pri { background: transparent; color: var(--text); border-color: var(--text); }
  /* soft: big rounded device cards with a check well and pastel status dots */
  .skin-soft .dlg { background: var(--bg); border: 1px solid var(--border); border-radius: var(--radius); }
  .skin-soft .sec { background: color-mix(in srgb, var(--accent) 30%, var(--card)); border-radius: 999px; align-self: flex-start; padding: 0 14px; font-size: 12px; }
  .skin-soft .sec.rec { background: color-mix(in srgb, var(--accent2) 30%, var(--card)); }
  .skin-soft .dev { background: var(--card); border: 1px solid var(--border); border-radius: 12px; }
  .skin-soft .dev.sel { box-shadow: 0 0 0 2px color-mix(in srgb, var(--accent) 45%, transparent); }
  .skin-soft .tg { border-radius: 8px; background: var(--sunken); border: 1px solid var(--border); }
  .skin-soft .tg.on { background: var(--success); color: var(--on-accent); border-color: transparent; }
  .skin-soft .dev .st::before { content: ""; display: inline-block; width: 6px; height: 6px; border-radius: 50%; background: var(--muted); margin-right: 6px; }
  .skin-soft .dev.ok .st::before { background: var(--success); } .skin-soft .dev.pend .st::before { background: var(--accent); }
  .skin-soft .btn { background: var(--card); border: 1px solid var(--border); border-radius: 999px; }
  .skin-soft .btn.pri { background: var(--accent); border-color: transparent; }
  /* rack: the patch bay; jacks as toggles, bay strips, mounting rails with fasteners */
  .skin-rack .dlg { background: var(--card); border: 1px solid var(--seam); border-radius: 3px; }
  .skin-rack .sec { background: color-mix(in srgb, var(--card) 70%, black); border: 1px solid #0a0c0e; border-radius: 2px; font-size: 10px; letter-spacing: 1.5px; text-transform: uppercase; color: var(--muted); }
  .skin-rack .dev { background: var(--card); border: 1px solid var(--seam); border-radius: 3px; box-shadow: inset 1px 1px 0 rgba(255,255,255,.08), inset -1px -1px 0 rgba(0,0,0,.35); }
  .skin-rack .dev.sel { border-color: var(--accent); }
  .skin-rack .tg { border-radius: 50%; background: radial-gradient(circle at 50% 50%, #05070a 0 5px, #3a4248 6px, #1b2126 9px); border: 1px solid #0a0c0e; }
  .skin-rack .tg.on { background: radial-gradient(circle at 50% 50%, #c9a45a 0 4px, #05070a 5px, #3a4248 6px, #1b2126 9px); }
  .skin-rack .dev .nm { font-size: 12px; }
  .skin-rack .dev .tag { border-radius: 2px; }
  .skin-rack .btn { background: linear-gradient(180deg, #2c333a, #1b2126); border: 1px solid #11161a; border-top-color: #3e474f; border-radius: 2px; text-transform: uppercase; letter-spacing: 1px; font-size: 11px; font-weight: 700; }
  [data-theme="light"] .skin-rack .btn { background: linear-gradient(180deg, var(--card-hover), var(--card)); border-color: var(--seam); }
  .skin-rack .btn.pri { color: var(--accent); border-color: var(--accent); }
  /* matrix: the target-acquisition board; port nodes on a bus trace, coordinates per port */
  .skin-matrix .dlg { background: var(--bg); border: 1px solid var(--border); font-family: var(--font); }
  .skin-matrix .sec { font-family: var(--mono); text-transform: uppercase; letter-spacing: 1px; font-size: 11px; color: var(--muted); border-bottom: 1px solid var(--border); }
  .skin-matrix .dev { border: 1px solid transparent; padding-left: 8px; }
  .skin-matrix .dev::before { content: ""; position: absolute; left: 18px; top: 0; bottom: 0; width: 1px; background: var(--border); }
  .skin-matrix .dev.sel { border-color: var(--accent); }
  .skin-matrix .tg { position: relative; width: 16px; height: 16px; border: 1px solid var(--border); background: var(--bg); }
  .skin-matrix .tg.on { background: var(--accent); border-color: var(--accent); }
  .skin-matrix .tg .co { position: absolute; left: 20px; font-family: var(--mono); font-size: 10px; letter-spacing: 1px; color: var(--muted); }
  .skin-matrix .dev .tag { border-radius: 0; }
  .skin-matrix .btn { background: var(--graph); border: 1px solid var(--border); }
  .skin-matrix .btn.pri { background: var(--accent); border-color: var(--accent); }
'''
PLAY = [('Speakers', 'TOPPING USB DAC', 'APO is already installed. Default device.', True, 'ok', ['DEFAULT'], 'OUT #1', 'sel'),
        ('CABLE Input', 'VB-Audio Virtual Cable', 'APO will be installed', True, 'pend', [], 'OUT #2', ''),
        ('Headphones', 'Realtek(R) Audio', 'APO can be installed', False, '', [], 'OUT #3', ''),
        ('Digital Output', 'NVIDIA High Definition Audio', 'APO cannot be installed. Disconnected.', False, '', [], 'OUT #4', 'off')]
REC = [('Microphone', 'USB Audio Device', 'APO is already installed (experimental). Default device.', True, 'ok', ['DEFAULT', 'EXP'], 'IN #1', ''),
       ('CABLE Output', 'VB-Audio Virtual Cable', 'APO can be installed (experimental)', False, '', ['EXP'], 'IN #2', '')]

def dev(skin, i, name, conn, status, on, cls, tags, port, extra, coord):
    tg_inner = ('[x]' if on else '[ ]') if skin == 'minimal' else ('✓' if on and skin == 'soft' else '')
    co = f'<span class="co">{coord}</span>' if skin == 'matrix' else ''
    num = f'<span style="color:var(--muted);margin-right:6px">{i:02d}</span>' if skin == 'minimal' else ''
    tagh = ''.join(f'<span class="tag">{t}</span>' for t in tags) + f'<span>{port}</span>'
    st_cls = 'st acc' if (skin == 'studio' and cls == 'pend') else 'st'
    return (f'<div class="dev {cls} {extra}"><span class="tg{" on" if on else ""}">{tg_inner}{co}</span>'
            f'<span class="nm">{num}{name}<small>{conn}</small></span><span class="tags">{tagh}</span><span class="{st_cls}">{status}</span></div>')

def dialog(skin):
    rows = ''
    n = 0
    for name, conn, status, on, cls, tags, port, extra in PLAY:
        n += 1; rows += dev(skin, n, name, conn, status, on, cls, tags, port, extra, f'P{n - 1}')
    play = f'<div class="sec">v Playback devices</div>{rows}'
    rows = ''
    for i, (name, conn, status, on, cls, tags, port, extra) in enumerate(REC):
        rows += dev(skin, i + 1, name, conn, status, on, cls, tags, port, extra, f'C{i}')
    rec = f'<div class="sec rec">v Recording devices</div>{rows}'
    return (f'<div class="dlg"><div class="ttl">Select the devices Equalizer APO should be installed to:</div>{play}{rec}'
            f'<div class="disc">&gt; Troubleshooting options (only when there are problems)</div>'
            f'<div class="btns"><span class="btn pri">OK</span><span class="btn">Cancel</span></div></div>')

frames = [frame(s, dialog(s), cls='') for s in SKINS]
page('DeviceSelector', 'Chrome', 1700, 'The install-target dialog: device rows with install toggles, sections, troubleshooting disclosure, OK and Cancel', CSS, frames, width=1680)
