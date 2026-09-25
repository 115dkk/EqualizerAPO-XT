"""ReferenceCard: the Include row's body (a reference to another file), found and missing (include_normal / include_missing_normal)."""
from rowlib import *

CSS = '''
  .rbody { display: flex; flex-direction: column; gap: 8px; }
  .line { display: flex; align-items: center; gap: 8px; height: 34px; }
  .name { font-weight: 600; font-size: 14px; }
  .name.mono { font-family: var(--mono); }
  .name.dim { color: var(--muted); font-weight: 500; }
  .well { display: flex; align-items: center; height: 28px; padding: 0 12px; font-family: var(--mono); font-size: 11.5px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .skin-list .crow + .crow { margin-top: 8px; }
  /* soft: the 34px rounded tile leads the row */
  .tile { width: 34px; height: 34px; border-radius: 10px; display: inline-flex; align-items: center; justify-content: center; color: var(--on-accent); flex: none; }
  .tile svg { width: 16px; height: 16px; }
  .tile.bang { font-weight: 800; font-size: 18px; }
  /* rack: the label strip (caption over name) and the bezel lamp */
  .strip { display: flex; flex-direction: column; justify-content: center; gap: 2px; min-width: 120px; }
  .strip .cap { font-size: 9px; letter-spacing: 1.5px; }
  .rlamp { width: 8px; height: 8px; border-radius: 50%; flex: none; box-shadow: 0 0 0 1.5px #0a0c0e, 0 0 0 2.5px #4a5257;
    background: radial-gradient(circle at 40% 35%, #fff8 0 1px, var(--accent2) 2px, color-mix(in srgb, var(--accent2) 60%, black)); }
  .rlamp.red { background: radial-gradient(circle at 40% 35%, #fff8 0 1px, var(--danger) 2px, color-mix(in srgb, var(--danger) 60%, black)); box-shadow: 0 0 0 1.5px #0a0c0e, 0 0 0 2.5px #4a5257, 0 0 6px var(--danger); }
  /* matrix: the marker cell opens the feed line */
  .marker { display: inline-flex; align-items: center; gap: 6px; height: 28px; padding: 0 10px; border: 1px solid var(--border); background: var(--graph); font-family: var(--mono); font-size: 11px; font-weight: 700; letter-spacing: 1px; color: var(--muted); }
  .marker.miss { color: var(--danger); border-color: var(--danger); }
  .skin-matrix .name { font-family: var(--mono); font-weight: 700; }
'''
PATH = 'C:\\Users\\example\\Documents\\EqualizerAPO\\config\\room\\example.txt'

def body(skin, missing):
    ib = lambda p, dis=False: f'<span class="ibtn{" dis" if dis else ""}">{svg(p)}</span>'
    if skin == 'studio':
        if not missing:
            return f'<div class="rbody"><div class="line"><span class="name">example.txt</span>{ib("folder")}{ib("pencil")}</div><div class="well">{PATH}</div></div>'
        return f'<div class="line"><span class="name dim">missing.txt</span><span class="chip" style="--k:var(--danger)">MISSING</span><span class="btn lit">{svg("folder")}Locate...</span>{ib("pencil", True)}</div>'
    if skin == 'minimal':
        if not missing:
            return f'<div class="line"><span class="name mono">example.txt</span><span class="btn">BROWSE</span><span class="btn">OPEN</span></div>'
        return f'<div class="line"><span class="name mono dim">missing.txt</span><span class="chip inv">MISSING</span><span class="btn lit">LOCATE</span><span class="btn dis">OPEN</span></div>'
    if skin == 'soft':
        if not missing:
            return f'<div class="line"><span class="tile" style="background:hsl(215 16% 62%)">{svg("include")}</span><span class="name">example.txt</span>{ib("folder")}{ib("pencil")}</div>'
        return f'<div class="line"><span class="tile bang" style="background:var(--danger)">!</span><span class="name">missing.txt</span><span class="btn lit">{svg("folder")}Locate...</span>{ib("pencil", True)}</div>'
    if skin == 'rack':
        if not missing:
            return f'<div class="line"><span class="rlamp"></span><div class="strip"><span class="cap">PATCH</span><span class="name" style="font-size:14px">example.txt</span></div>{ib("folder")}{ib("pencil")}</div>'
        return f'<div class="line"><span class="rlamp red"></span><div class="strip"><span class="cap">PATCH <span style="color:var(--accent);margin-left:6px">NOT FOUND</span></span><span class="name dim" style="font-size:14px">missing.txt</span></div><span class="btn lit">LOCATE</span>{ib("pencil", True)}</div>'
    if skin == 'matrix':
        if not missing:
            return f'<div class="line"><span class="marker">&gt; SRC</span><span class="name">example.txt</span>{ib("folder")}{ib("pencil")}</div>'
        return f'<div class="line"><span class="marker miss">MISSING</span><span class="name">missing.txt</span><span class="btn lit">{svg("folder")}LOCATE</span>{ib("pencil", True)}</div>'

frames = [frame(s, row(s, 'include', 5, 'Include', body(s, False)) + row(s, 'include', 7, 'Include', body(s, True))) for s in SKINS]
page('ReferenceCard', 'Rows', 1200, 'The Include row body: a found reference with its path window, and a missing one, five skins', CSS, frames)
