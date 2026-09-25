"""LogicRows: If/ElseIf/EndIf and Eval rows with each skin's scope gutter (logic_normal): the gate beam, the indent
guides and watch column, the pastel arm, the relay power bus, the printed brackets."""
from rowlib import *
from gen_knob import knob

CSS = '''
  .lrow + .lrow, .scope + .lrow, .lrow + .scope, .scope + .scope { margin-top: 6px; }
  .scope { position: relative; padding-left: var(--indent); }
  .scope > .lrow + .lrow, .scope > .scope + .lrow, .scope > .lrow + .scope { margin-top: 6px; }
  .gb { display: flex; align-items: center; gap: 12px; height: 60px; }
  .gb .vbox { min-width: 150px; height: 34px; }
  .gb .prm { display: flex; flex-direction: column; gap: 4px; }
  .skin-list { --indent: 24px; }
  /* studio: the gate beam flows down the gutter; stations are anchor dots; a false branch is a dimmed afterglow */
  .skin-studio .scope::before { content: ""; position: absolute; left: 8px; top: -4px; bottom: 10px; width: 2px; border-radius: 1px;
    background: linear-gradient(180deg, var(--accent), var(--accent) 85%, transparent); box-shadow: 0 0 4px var(--accent), 0 0 9px color-mix(in srgb, var(--accent) 45%, transparent); }
  .skin-studio .scope.skipped::before { opacity: .25; box-shadow: none; }
  .skin-studio .scope.skipped > .lrow { opacity: .55; }
  .skin-studio .station { position: absolute; left: -17px; top: 17px; width: 5px; height: 5px; border-radius: 50%; background: var(--accent); box-shadow: 0 0 0 3px color-mix(in srgb, var(--accent) 35%, transparent), 0 0 6px var(--accent); }
  .skin-studio .station.off { background: color-mix(in srgb, var(--muted) 50%, var(--bg)); box-shadow: none; }
  .skin-studio .crow .readout { color: var(--text); opacity: .55; }
  /* minimal: an indent guide per scope level, dotted where the engine did not run; the watch register at the right */
  .skin-minimal .scope::before { content: ""; position: absolute; left: 6px; top: -3px; bottom: 0; width: 1px; background: var(--border); }
  .skin-minimal .scope.skipped::before { background: repeating-linear-gradient(180deg, var(--border) 0 3px, transparent 3px 6px); }
  .skin-minimal .scope.skipped > .lrow .crow { background: var(--surface); }
  .skin-minimal .scope.skipped > .lrow .crow .title, .skin-minimal .scope.skipped > .lrow .crow .summary { color: var(--muted); }
  .skin-minimal .crow .readout { font-weight: 700; color: var(--text); letter-spacing: 1px; }
  .skin-minimal .crow .readout.dim { color: var(--muted); font-weight: 500; }
  .skin-minimal .crow .readout.dash { color: color-mix(in srgb, var(--muted) 60%, var(--bg)); }
  /* soft: the pastel arm holds the block, rounded fingertip and cap; a sleeping section relaxes to the resting pastel */
  .skin-soft .scope::before { content: ""; position: absolute; left: 6px; top: -2px; bottom: 6px; width: 4px; border-radius: 999px; background: color-mix(in srgb, var(--accent) 75%, var(--card)); }
  .skin-soft .scope.skipped::before { background: color-mix(in srgb, var(--accent) 20%, var(--card)); }
  /* rack: the relay power bus: a dark casing with an amber core; jewel lamps report the branches */
  .skin-rack .scope::before { content: ""; position: absolute; left: 4px; top: -3px; bottom: 4px; width: 6px; background: var(--seam); border-radius: 1px; box-shadow: inset 0 0 0 1px rgba(255,255,255,.05); }
  .skin-rack .scope::after { content: ""; position: absolute; left: 6px; top: -1px; bottom: 6px; width: 2px; background: var(--accent); box-shadow: 0 0 4px var(--accent); }
  .skin-rack .scope.skipped::after { background: color-mix(in srgb, var(--accent) 30%, var(--card)); box-shadow: none; }
  .skin-rack .jewel { position: absolute; left: -19px; top: 17px; width: 6px; height: 6px; border-radius: 50%; box-shadow: 0 0 0 1px #0a0c0e, 0 0 0 2px #4a5257, 0 0 5px var(--accent2);
    background: radial-gradient(circle at 40% 35%, #fff8 0 1px, var(--accent2) 2px, color-mix(in srgb, var(--accent2) 60%, black)); }
  .skin-rack .jewel.off { background: radial-gradient(circle at 40% 35%, #fff2 0 1px, #2a2622 2px, #15120f); box-shadow: 0 0 0 1px #0a0c0e, 0 0 0 2px #33393e; }
  /* matrix: printed brackets; a gate lamp per decision; a skipped row is a cancelled posting */
  .skin-matrix .scope::before { content: ""; position: absolute; left: 11px; top: -3px; bottom: 0; width: 1px; background: var(--muted); opacity: .8; }
  .skin-matrix .scope::after { content: ""; position: absolute; left: 11px; bottom: 0; width: 12px; height: 1px; background: var(--muted); opacity: .8; }
  .skin-matrix .scope.skipped > .lrow .crow { border-style: dotted; }
  .skin-matrix .scope.skipped > .lrow .crow::before { background: var(--border); }
  .skin-matrix .scope.skipped > .lrow .crow .title, .skin-matrix .scope.skipped > .lrow .crow .summary { opacity: .55; }
  .skin-matrix .scope.skipped > .lrow .crow .lampdot { background: transparent; border: 1px solid var(--success); box-sizing: border-box; }
  .skin-matrix .gate { position: absolute; left: 10px; top: 17px; width: 5px; height: 5px; background: var(--success); }
  .skin-matrix .gate.off { background: transparent; border: 1px solid var(--success); box-sizing: border-box; }
  .skin-matrix .gate.none { background: transparent; border: 1px solid var(--muted); box-sizing: border-box; }
  .skin-matrix .crow .readout { border: 1px solid var(--border); background: var(--graph); padding: 2px 8px; color: var(--text); font-weight: 700; }
'''
SENT = {'if1': ('outputChannelCount >= 6', 'If outputChannelCount is at least 6'),
        'if2': ('sampleRate > 48000', 'If sampleRate is more than 48000'),
        'elif': ('outputChannelCount == 4', 'Otherwise, if outputChannelCount is 4'),
        'end': ('', 'End of the rule'), 'eval': ('bassBoost = 6', 'Set bassBoost to 6')}

def sentence(skin, key): return SENT[key][1] if skin == 'soft' else SENT[key][0]

def lrow(skin, kind, num, title, summary='', body='', readout='', collapsed=True, decision=None, pict=None):
    marker = ''
    if decision is not None:
        if skin == 'studio': marker = f'<span class="station{"" if decision == "true" else " off"}"></span>'
        elif skin == 'rack': marker = f'<span class="jewel{"" if decision == "true" else " off"}"></span>'
        elif skin == 'matrix': marker = f'<span class="gate {{"true": "", "false": "off", "none": "none"}}[decision]"></span>'.replace('{"true": "", "false": "off", "none": "none"}[decision]', {"true": "", "false": "off", "none": "none"}[decision])
    r = row(skin, kind, num, title, body, summary=summary, collapsed=collapsed, readout=readout, pict=pict, power=True)
    if skin == 'matrix' and decision is not None:
        r = r.replace('<span class="lampdot"></span>', '')  # the gate lamp replaces the signal lamp on decision rows
    return f'<div class="lrow" style="position:relative">{marker}{r}</div>'

def preamp_body(skin, value):
    return f'<div class="gb">{knob(skin, True, -0.25, 48)}<div class="prm"><span class="cap">Gain</span><span class="vbox">{value}</span></div></div>'

def build(skin):
    watch = lambda t, cls='': (f'<span class="readout {cls}">{t}</span>' if skin == 'minimal' else '')
    ro_eval = '= 6'
    out = [lrow(skin, 'eval', 1, 'Eval', sentence(skin, 'eval'), readout=ro_eval),
           lrow(skin, 'if', 2, 'If', sentence(skin, 'if1'), readout=('TRUE' if skin == 'minimal' else ''), decision='true')]
    inner = [lrow(skin, 'preamp', 3, 'Preamp', body=preamp_body(skin, '-3.0 dB'), collapsed=False),
             lrow(skin, 'if', 4, 'If', sentence(skin, 'if2'), readout=('FALSE' if skin == 'minimal' else ''), decision='false')]
    nested = [lrow(skin, 'delay', 5, 'Delay', 'Time 0.25 ms'), lrow(skin, 'if', 6, 'End if', sentence(skin, 'end') if skin == 'soft' else '')]
    if skin == 'minimal':
        inner.append(f'<div class="scope skipped">{nested[0]}</div>'); inner.append(nested[1])
    else:
        inner.append(f'<div class="scope skipped">{"".join(nested)}</div>')
    out.append(f'<div class="scope">{"".join(inner)}</div>')
    out.append(lrow(skin, 'if', 7, 'Else if', sentence(skin, 'elif'), readout=('—' if skin == 'minimal' else ''), decision='none'))
    tail = [lrow(skin, 'preamp', 8, 'Preamp', 'Gain -1.5 dB'), lrow(skin, 'if', 9, 'End if', sentence(skin, 'end') if skin == 'soft' else '')]
    if skin == 'minimal':
        out.append(f'<div class="scope skipped">{tail[0]}</div>'); out.append(tail[1])
    else:
        out.append(f'<div class="scope skipped">{"".join(tail)}</div>')
    html = ''.join(out)
    if skin == 'minimal':
        html = html.replace('<span class="readout ">TRUE</span>', '<span class="readout">TRUE</span>')
    return html

frames = []
for s in SKINS:
    ind = {'studio': 18, 'minimal': 16, 'soft': 20, 'rack': 16, 'matrix': 24}[s]
    f = frame(s, build(s)).replace('class="skin skin-' + s + ' skin-list"', f'class="skin skin-{s} skin-list" style="--indent:{ind}px"')
    frames.append(f)
page('LogicRows', 'Rows', 2720, 'If, Else if, End if and Eval rows with each skin scope gutter: gate beam, indent guides, pastel arm, relay bus, printed brackets', CSS, frames)
