"""VSTBus: the VST row with the forced-negotiation slot-fill rails, the reference line and the IN/OUT bus strip
(vst_slotfill_normal: the plug-in file is missing, the rails and busses stand)."""
from rowlib import *

CSS = '''
  .vb { display: flex; flex-direction: column; gap: 8px; }
  .rail { display: flex; align-items: center; gap: 10px; height: 30px; padding: 0 10px; }
  .rail .role { font-size: 10px; font-weight: 700; letter-spacing: 1px; color: var(--muted); }
  .rail .cellv { display: inline-flex; align-items: center; gap: 6px; height: 22px; padding: 0 6px; font-family: var(--mono); font-size: 11.5px; font-weight: 700; color: var(--text); }
  .rail .cellv.dash { color: var(--muted); }
  .rail .chev { border-left-width: 3px; border-right-width: 3px; border-top-width: 4px; }
  .rail .grp { display: inline-flex; align-items: center; gap: 4px; }
  .latch { display: inline-flex; align-items: center; gap: 6px; height: 22px; padding: 0 9px; font-size: 10px; font-weight: 700; letter-spacing: 1px; margin-right: 6px; }
  .latch .dot { width: 6px; height: 6px; border-radius: 50%; background: var(--accent); box-shadow: 0 0 5px var(--accent); }
  .line { display: flex; align-items: center; gap: 8px; height: 34px; }
  .name { font-weight: 600; font-size: 14px; }
  .name.dim { color: var(--muted); font-weight: 500; }
  .bus { display: inline-flex; align-items: center; gap: 8px; }
  .bus .bcell { display: inline-flex; align-items: center; gap: 8px; height: 26px; padding: 0 8px; font-family: var(--mono); font-size: 11.5px; font-weight: 700; color: var(--text); }
  .bus .bcap { font-size: 9.5px; font-weight: 700; letter-spacing: 1px; color: var(--muted); }
  .bus .n { color: var(--muted); font-weight: 500; }
  .arrow { width: 14px; height: 10px; color: var(--muted); }
  .skin-list .crow + .crow { margin-top: 8px; }
  /* studio: sunken console bands, scribble-strip cells, the lit console switch */
  .skin-studio .rail { background: color-mix(in srgb, var(--sunken) 70%, transparent); border-top: 1px solid var(--border); border-bottom: 1px solid var(--border); }
  .skin-studio .rail .cellv { background: var(--sunken); border: 1px solid var(--border); border-radius: 4px; }
  .skin-studio .latch { background: color-mix(in srgb, var(--accent) 15%, transparent); border: 1px solid color-mix(in srgb, var(--accent) 42%, transparent); border-radius: 999px; color: var(--accent); }
  .skin-studio .bus { background: var(--sunken); border: 1px solid var(--border); border-top-color: rgba(0,0,0,.55); border-radius: var(--radius); padding: 4px 10px; }
  .skin-studio .bus .bcell { background: color-mix(in srgb, var(--card-hover) 60%, transparent); border: 1px solid var(--border); border-radius: 6px; }
  /* minimal: ink only, no frame; fill is the reverse-video token */
  .skin-minimal .rail { padding: 0 2px; }
  .skin-minimal .rail .role { text-transform: lowercase; letter-spacing: 0; font-family: var(--mono); font-size: 11px; }
  .skin-minimal .latch { background: var(--text); color: var(--surface); text-transform: lowercase; letter-spacing: 0; font-family: var(--mono); font-size: 11px; padding: 0 6px; height: 18px; }
  .skin-minimal .latch .dot { display: none; }
  .skin-minimal .name { font-family: var(--mono); }
  .skin-minimal .bus .bcell { padding: 0 2px; }
  .skin-minimal .bus .bcap { text-transform: lowercase; letter-spacing: 0; font-family: var(--mono); font-size: 11px; font-weight: 500; }
  .skin-minimal .arrow { display: none; }
  /* soft: a pastel-washed tray hugging outline pills; the dot toggle */
  .skin-soft .rail { background: color-mix(in srgb, var(--accent) 10%, var(--surface)); border-radius: 999px; }
  .skin-soft .rail .cellv { border: 1px solid color-mix(in srgb, var(--accent) 45%, var(--border)); border-radius: 999px; font-family: var(--font); font-weight: 600; }
  .skin-soft .rail .cellv.dash { border-style: dotted; }
  .skin-soft .latch { color: var(--text); font-weight: 600; text-transform: none; letter-spacing: 0; font-size: 11.5px; }
  .skin-soft .latch .dot { background: var(--accent); box-shadow: none; width: 8px; height: 8px; }
  .skin-soft .bus .bcell { background: var(--card); border: 1px solid var(--border); border-radius: 999px; font-family: var(--font); }
  .tile { width: 34px; height: 34px; border-radius: 10px; display: inline-flex; align-items: center; justify-content: center; color: var(--on-accent); flex: none; font-weight: 800; font-size: 18px; }
  /* rack: the patch strip sunk into the face, caps everywhere, the pilot lamp on the FILL cap */
  .skin-rack .rail { background: color-mix(in srgb, var(--card) 70%, black); border: 1px solid #0a0c0e; border-radius: 3px; box-shadow: inset 0 2px 3px rgba(0,0,0,.6), 0 1px 0 rgba(255,255,255,.06); }
  .skin-rack .rail .cellv, .skin-rack .latch, .skin-rack .bus .bcell { background: linear-gradient(180deg, #2c333a, #1b2126); border: 1px solid #11161a; border-top-color: #3e474f; border-radius: 3px; }
  [data-theme="light"] .skin-rack .rail .cellv, [data-theme="light"] .skin-rack .latch, [data-theme="light"] .skin-rack .bus .bcell { background: linear-gradient(180deg, var(--card-hover), var(--card)); border-color: var(--seam); border-top-color: #fff8; }
  .skin-rack .latch { color: var(--text); }
  .skin-rack .latch .dot { background: radial-gradient(circle at 40% 35%, #fff8 0 1px, var(--accent2) 2px, color-mix(in srgb, var(--accent2) 60%, black)); box-shadow: 0 0 0 1px #0a0c0e, 0 0 4px var(--accent2); }
  .skin-rack .strip { display: flex; flex-direction: column; justify-content: center; gap: 2px; min-width: 130px; }
  .skin-rack .strip .cap { font-size: 8px; letter-spacing: 1.5px; }
  .rlamp { width: 8px; height: 8px; border-radius: 50%; flex: none; box-shadow: 0 0 0 1.5px #0a0c0e, 0 0 0 2.5px #4a5257, 0 0 6px var(--danger);
    background: radial-gradient(circle at 40% 35%, #fff8 0 1px, var(--danger) 2px, color-mix(in srgb, var(--danger) 60%, black)); }
  /* matrix: postings under a board rule; the gate cell; the port strip */
  .skin-matrix .rail { border-top: 1px solid var(--border); padding: 0 4px; }
  .skin-matrix .rail .cellv { border-bottom: 1px solid var(--border); border-radius: 0; padding: 0 4px; }
  .skin-matrix .latch { border: 1px solid var(--accent); color: var(--text); background: color-mix(in srgb, var(--accent) 10%, var(--bg)); font-family: var(--mono); }
  .skin-matrix .latch .dot { display: none; }
  .skin-matrix .name { font-family: var(--mono); font-weight: 700; }
  .skin-matrix .bus .bcell { border: 1px solid var(--border); background: var(--graph); }
  .skin-matrix .port { display: flex; align-items: center; justify-content: space-between; height: 24px; padding: 0 8px; font-family: var(--mono); font-size: 11px; font-weight: 700; letter-spacing: 2px; color: var(--muted); border-bottom: 1px solid var(--border); }
  .marker { display: inline-flex; align-items: center; gap: 6px; height: 28px; padding: 0 10px; border: 1px solid var(--danger); background: var(--graph); font-family: var(--mono); font-size: 11px; font-weight: 700; letter-spacing: 1px; color: var(--danger); }
'''
ARROW = '<svg class="arrow" viewBox="0 0 14 10" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M1 5h11"/><path d="M8 1.5L12 5 8 8.5"/></svg>'
IN_SLOTS = [('L', 'L'), ('R', 'R'), ('C', 'C'), ('LFE', '-'), ('RL', 'SL'), ('RR', 'SR')]
OUT_SLOTS = [('L', 'L'), ('R', 'R'), ('C', 'C'), ('LFE', 'LFE'), ('RL', 'RL'), ('RR', 'RR')]

def rail(skin, slots, latch=None):
    cells = ''.join(f'<span class="grp"><span class="role">{r}</span><span class="cellv{" dash" if v == "-" else ""}">{v}<span class="chev"></span></span></span>' for r, v in slots)
    latch_html = f'<span class="latch"><span class="dot"></span>{latch}</span>' if latch else ''
    return f'<div class="rail">{latch_html}{cells}</div>'

def bus(skin):
    if skin == 'minimal':
        return f'<span class="bus"><span class="bcell"><span class="bcap">in</span>5.1<span class="n">:6</span><span class="chev"></span></span><span class="bcap">-&gt; out</span><span class="bcell">5.1<span class="n">:6</span><span class="chev"></span></span></span>'
    sep = ':' if skin == 'matrix' else ' '
    return (f'<span class="bus"><span class="bcell"><span class="bcap">IN</span>5.1<span class="n">{sep}6</span><span class="chev"></span></span>{ARROW}'
            f'<span class="bcell"><span class="bcap">OUT</span>5.1<span class="n">{sep}6</span><span class="chev"></span></span></span>')

def body(skin):
    ib = lambda label, lit=False, dis=False: f'<span class="btn{" lit" if lit else ""}{" dis" if dis else ""}">{svg("folder") if lit else ""}{label}</span>'
    if skin == 'studio':
        line = f'<div class="line"><span class="name">example.vst3</span><span class="chip" style="--k:var(--danger)">MISSING</span>{ib("Locate...", True)}{ib("Open panel")}{ib("...")}</div>'
        return f'<div class="vb">{rail(skin, IN_SLOTS, "FILL")}{line}<div class="line">{bus(skin)}</div>{rail(skin, OUT_SLOTS)}</div>'
    if skin == 'minimal':
        line = f'<div class="line"><span class="name">example.vst3</span><span class="chip inv">MISSING</span>{bus(skin)}<span class="btn lit">LOCATE</span><span class="btn dis">PANEL</span><span class="btn dis">OPT</span></div>'
        return f'<div class="vb">{rail(skin, IN_SLOTS, "fill")}{line}{rail(skin, OUT_SLOTS)}</div>'
    if skin == 'soft':
        line = f'<div class="line"><span class="tile" style="background:var(--danger)">!</span><span class="name">example.vst3</span>{bus(skin)}{ib("Locate...", True)}{ib("Open panel")}{ib("...")}</div>'
        return f'<div class="vb">{rail(skin, IN_SLOTS, "Fill")}{line}{rail(skin, OUT_SLOTS)}</div>'
    if skin == 'rack':
        line = (f'<div class="line"><span class="rlamp"></span><div class="strip"><span class="cap">MODULE <span style="color:var(--accent);margin-left:6px">NOT FOUND</span></span>'
                f'<span class="name dim" style="font-size:12.5px">example.vst3</span></div>{bus(skin)}<span class="btn lit">LOCATE</span><span class="btn dis">OPEN PANEL</span><span class="btn">...</span></div>')
        return f'<div class="vb">{rail(skin, IN_SLOTS, "FILL")}{line}{rail(skin, OUT_SLOTS)}</div>'
    if skin == 'matrix':
        port = '<div class="port"><span>&gt; IN</span><span>EXTERNAL DEVICE</span><span>OUT &gt;</span></div>'
        line = f'<div class="line"><span class="marker">MISSING</span><span class="name">example.vst3</span>{bus(skin)}<span class="btn lit">{svg("folder")}LOCATE</span><span class="btn dis">Open panel</span><span class="btn">...</span></div>'
        return f'<div class="vb">{rail(skin, IN_SLOTS, "FILL")}{port}{line}{rail(skin, OUT_SLOTS)}</div>'

frames = [frame(s, row(s, 'vst', 9, 'VST Plugin', body(s))) for s in SKINS]
page('VSTBus', 'Rows', 1240, 'The VST row: slot-fill rails, the reference line with IN/OUT bus strip, five skins', CSS, frames)
