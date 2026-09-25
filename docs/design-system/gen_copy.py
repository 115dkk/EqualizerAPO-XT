"""CopyRouting: the Copy row body in five renderers (copy_normal: VC=0.5*L+0.5*R, R=L)."""
from rowlib import *

CSS = '''
  .cbody { position: relative; }
  .pill { display: inline-flex; align-items: center; justify-content: center; min-width: 28px; height: 22px; padding: 0 7px; box-sizing: border-box; font-family: var(--mono); font-size: 12px; font-weight: 700; border-radius: 6px;
    color: var(--c); background: color-mix(in srgb, var(--c) 28%, transparent); border: 1px solid color-mix(in srgb, var(--c) 70%, transparent); }
  .pill.virt { border-style: dashed; background: color-mix(in srgb, var(--c) 14%, transparent); }
  .pill.ghost { color: var(--accent); border: 1px dashed color-mix(in srgb, var(--accent) 45%, transparent); background: transparent; font-size: 15px; font-weight: 400; }
  /* studio: light traces etched on the glass between the input row and the output row */
  .lt { position: relative; height: 118px; width: 220px; }
  .lt .pill { position: absolute; }
  .lt svg { position: absolute; left: 0; top: 0; overflow: visible; }
  .lt .fac { position: absolute; font-family: var(--mono); font-size: 11px; font-weight: 500; color: var(--muted); background: var(--bg); border-radius: 4px; padding: 2px 5px; }
  /* minimal: the numbered step list, a console session */
  .steps { font-family: var(--mono); font-size: 13px; background: color-mix(in srgb, var(--surface) 70%, var(--bg)); }
  .steps .r { display: flex; align-items: center; height: 30px; gap: 10px; }
  .steps .r.h { color: var(--muted); height: 26px; }
  .steps .arw { color: var(--muted); }
  .steps .plus { color: var(--success); font-weight: 700; }
  .steps .no { width: 28px; color: var(--muted); border-right: 1px solid var(--border); height: 100%; display: inline-flex; align-items: center; }
  .steps .tok { font-weight: 700; color: var(--c); }
  .steps .tok.virt { border: 1px dashed var(--c); padding: 0 4px; }
  .steps .fx { background: var(--sunken); padding: 1px 4px; color: var(--text); font-size: 12px; }
  .steps .tgt { border: 1px solid var(--border); padding: 0 5px; color: var(--muted); font-size: 12px; }
  .steps .cur { display: inline-block; width: 8px; height: 15px; background: var(--accent); }
  /* soft: formula blocks */
  .blk { position: relative; display: flex; align-items: center; gap: 8px; height: 44px; padding: 0 14px 0 20px; margin-bottom: 8px; border-radius: 12px; background: var(--card); border: 1px solid color-mix(in srgb, var(--accent) 40%, var(--border)); }
  .blk::before { content: ""; position: absolute; left: 6px; top: 8px; bottom: 8px; width: 4px; border-radius: 2px; background: var(--c); }
  .blk .dst { font-weight: 700; font-size: 15px; color: var(--c); margin-right: 6px; }
  .blk .op { color: var(--muted); margin: 0 4px; }
  .blk .term { display: inline-flex; align-items: center; height: 26px; padding: 0 9px; box-sizing: border-box; border-radius: 999px; font-size: 14px; color: var(--text);
    background: color-mix(in srgb, var(--c) 16%, transparent); border: 1px solid color-mix(in srgb, var(--c) 47%, transparent); }
  .blk .term b { color: var(--c); font-weight: 600; }
  .blk .add, .addch { display: inline-flex; align-items: center; justify-content: center; height: 26px; padding: 0 10px; box-sizing: border-box; border-radius: 999px; border: 1px dashed color-mix(in srgb, var(--accent) 45%, var(--border)); color: var(--accent); font-size: 14px; }
  .blk .add { width: 32px; padding: 0; }
  /* rack: the routing matrix button field on a sunken sub-panel */
  .field { display: block; margin-right: 8px; padding: 12px 16px 12px 12px; border-radius: 3px; background: color-mix(in srgb, var(--card) 60%, black); box-shadow: inset 0 3px 4px rgba(0,0,0,.65), inset 0 -1px 0 rgba(255,255,255,.08); }
  .field table { border-collapse: separate; border-spacing: 10px 10px; }
  .field th { font-size: 12px; letter-spacing: .5px; font-weight: 700; color: var(--c); text-transform: uppercase; padding: 0 4px; }
  .field tr > th:first-child { text-align: right; padding-right: 14px; }
  .field .capb { width: 46px; height: 30px; border-radius: 3px; display: inline-flex; align-items: center; justify-content: center; background: linear-gradient(180deg, #2c333a, #1b2126); border: 1px solid #11161a; border-top-color: #3e474f; font-family: var(--mono); font-size: 12px; font-weight: 700; color: var(--text); }
  [data-theme="light"] .field .capb { background: linear-gradient(180deg, var(--card-hover), var(--card)); border-color: var(--seam); }
  .field .capb.on { border-color: var(--accent); background: linear-gradient(180deg, #1b2126, #2c333a); box-shadow: inset 0 1px 2px rgba(0,0,0,.6), 0 0 4px color-mix(in srgb, var(--accent) 40%, transparent); }
  .field .capb .dot { width: 6px; height: 6px; border-radius: 50%; background: var(--accent); box-shadow: 0 0 5px var(--accent); }
  .field .capb .dimple { width: 3px; height: 3px; border-radius: 50%; background: rgba(0,0,0,.7); box-shadow: 0 1px 0 rgba(255,255,255,.08); }
  .field .addcap { margin-top: 8px; height: 24px; padding: 0 10px; display: inline-flex; align-items: center; border-radius: 3px; background: linear-gradient(180deg, #2c333a, #1b2126); border: 1px solid #11161a; border-top-color: #3e474f; font-size: 11px; letter-spacing: 1px; font-weight: 700; color: var(--text); }
  /* matrix: the crosspoint grid */
  .grid { border-collapse: separate; border-spacing: 4px; font-family: var(--mono); font-size: 12px; font-weight: 700; }
  .grid .hd { display: inline-flex; align-items: center; justify-content: center; min-width: 44px; height: 18px; color: var(--on-accent); background: var(--c); font-size: 12px; letter-spacing: 0; }
  .grid .hd.virt { background: color-mix(in srgb, var(--c) 10%, transparent); color: var(--c); border: 1px dashed var(--c); }
  .grid .rh { justify-content: center; }
  .grid .cell { width: 52px; height: 24px; display: inline-flex; align-items: center; justify-content: center; border: 1px solid var(--border); background: var(--graph); color: var(--text); }
  .grid .cell.acc { border-color: var(--accent); color: var(--accent); }
  .grid .cell.one { border-color: var(--success); color: var(--success); }
  .mbus { display: inline-flex; align-items: center; height: 20px; padding: 0 8px; border: 1px solid var(--border); background: var(--graph); color: var(--muted); font-family: var(--mono); font-weight: 700; font-size: 10.67px; letter-spacing: 1px; margin-top: 6px; }
'''

def studio():
    L, R, VC = CHANNEL['L'], CHANNEL['R'], CHANNEL['C']
    # ports: inputs at y 10, outputs at y 96; x centres 24 / 62 / 100
    trace = lambda x0, x1: f'<path d="M {x0} 30 C {x0} 62, {x1} 62, {x1} 96" fill="none" stroke="var(--accent)" stroke-width="{{w}}" stroke-opacity="{{o}}"/>'
    paths = ''
    for x0, x1 in ((24, 24), (62, 24), (24, 62)):
        for w, o in ((7, .05), (4, .14), (2, .5), (1.2, 1)):
            paths += trace(x0, x1).format(w=w, o=o)
    return (f'<div class="lt"><svg width="220" height="118">{paths}'
            f'<circle cx="24" cy="96" r="2.2" fill="var(--accent)"/><circle cx="62" cy="96" r="2.2" fill="var(--accent)"/></svg>'
            f'<span class="pill" style="--c:{L};left:10px;top:8px">L</span><span class="pill" style="--c:{R};left:48px;top:8px">R</span>'
            f'<span class="fac" style="left:18px;top:50px">0.5</span><span class="fac" style="left:38px;top:70px">0.5</span>'
            f'<span class="pill virt" style="--c:{VC};left:5px;top:94px">VC</span><span class="pill" style="--c:{R};left:48px;top:94px">R</span>'
            f'<span class="pill ghost" style="left:86px;top:94px;width:30px">+</span></div>')

def minimal():
    ink = MINIMAL_INK
    arw = '<span class="arw">&larr;</span>'
    return (f'<div class="steps"><div class="r h"><span class="no">#</span>DEST<span style="margin-left:40px">SOURCES</span></div>'
            f'<div class="r"><span class="no">01</span><span class="tok virt" style="--c:{ink["C"]}">VC</span>{arw}'
            f'<span class="tok" style="--c:{ink["L"]}">L</span><span class="fx">×0.5</span><span class="plus">+</span><span class="tok" style="--c:{ink["R"]}">R</span><span class="fx">×0.5</span><span class="tgt">+</span></div>'
            f'<div class="r"><span class="no">02</span><span class="tok" style="--c:{ink["R"]}">R</span>{arw}<span class="tok" style="--c:{ink["L"]}">L</span><span class="tgt">+</span></div>'
            f'<div class="r"><span class="no">&gt;</span><span class="cur"></span></div></div>')

def soft():
    # the formula blocks wear the identity colours (CopyRoutingAdapter::channelColor), not pastels
    h = lambda ch: CHANNEL[ch]
    return (f'<div class="blk" style="--c:{h("C")}"><span class="dst">VC</span><span class="op">=</span><span class="term" style="--c:{h("L")}">0.5·<b>L</b></span><span class="op">+</span>'
            f'<span class="term" style="--c:{h("R")}">0.5·<b>R</b></span><span class="add">+</span></div>'
            f'<div class="blk" style="--c:{h("R")}"><span class="dst">R</span><span class="op">=</span><span class="term" style="--c:{h("L")}"><b>L</b></span><span class="add">+</span></div>'
            f'<span class="addch">+ Add channel</span>')

def rack():
    L, R, VC = CHANNEL['L'], CHANNEL['R'], CHANNEL['C']
    cap = lambda inner, on=False: f'<span class="capb{" on" if on else ""}">{inner}</span>'
    return (f'<div class="field"><table><tr><th></th><th style="--c:{L}">L</th><th style="--c:{R}">R</th><th style="--c:{VC}">VC</th></tr>'
            f'<tr><th style="--c:{VC}">VC</th><td>{cap("0.5", True)}</td><td>{cap("0.5", True)}</td><td>{cap("<span class=dimple></span>")}</td></tr>'
            f'<tr><th style="--c:{R}">R</th><td>{cap("<span class=dot></span>", True)}</td><td>{cap("<span class=dimple></span>")}</td><td>{cap("<span class=dimple></span>")}</td></tr></table>'
            f'<span class="addcap">ADD</span></div>')

def matrix():
    L, R, VC = CHANNEL['L'], CHANNEL['R'], CHANNEL['C']
    return (f'<table class="grid"><tr><td></td><td><span class="hd" style="--c:{L}">L</span></td><td><span class="hd" style="--c:{R}">R</span></td><td><span class="hd virt" style="--c:{VC}">VC</span></td></tr>'
            f'<tr><td><span class="hd virt rh" style="--c:{VC}">VC</span></td><td><span class="cell acc">0.5</span></td><td><span class="cell acc">0.5</span></td><td><span class="cell"></span></td></tr>'
            f'<tr><td><span class="hd rh" style="--c:{R}">R</span></td><td><span class="cell one">1</span></td><td><span class="cell"></span></td><td><span class="cell"></span></td></tr></table>'
            f'<span class="mbus">+BUS</span>')

BODY = {'studio': studio, 'minimal': minimal, 'soft': soft, 'rack': rack, 'matrix': matrix}
frames = [frame(s, row(s, 'copy', 16, 'Copy', BODY[s](), chans=('R',))) for s in SKINS]
page('CopyRouting', 'Rows', 1290, 'The Copy row body: light traces, step list, formula blocks, patchbay button field, crosspoint grid', CSS, frames)
