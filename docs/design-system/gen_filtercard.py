"""FilterCard: the BiQuad (peaking) row as the gallery renders it (filter_normal: 948x136..145)."""
from rowlib import *
from gen_knob import knob

CSS = '''
  .pbody { display: flex; align-items: center; gap: 12px; height: 78px; }
  .grp { display: flex; align-items: center; gap: 10px; }
  .prm { display: flex; flex-direction: column; gap: 4px; }
  .prm .vbox { min-width: 150px; height: 34px; }
  .prm .sel { min-width: 150px; height: 26px; }
  .prm .cap { display: inline-flex; align-items: center; height: 26px; padding-left: 2px; text-transform: none; letter-spacing: 0; font-size: 12.5px; font-weight: 600; color: var(--text); }
  .fsel { min-width: 170px; height: 34px; color: var(--text); font-weight: 600; }
  .skin-minimal .fsel { font-family: var(--mono); font-weight: 500; }
  .skin-minimal .prm .vbox { background: transparent; border: 0; color: var(--ink-bright); padding-left: 2px; }
  .skin-minimal .prm .cap { font-family: var(--mono); color: var(--muted); }
  /* rack: parameter captions are translated UI strings, engraved as written (no uppercasing, no tracking) */
  .skin-rack .prm .sel, .skin-rack .prm .cap { font-size: 12px; font-weight: 700; }
  .skin-rack .prm .sel { color: var(--muted); font-weight: 600; }
  .skin-rack .prm .cap { color: var(--text); }
  .skin-matrix .prm .cap { text-transform: uppercase; letter-spacing: 1px; font-size: 10.67px; color: var(--muted); }
  .skin-matrix .fsel { text-transform: none; letter-spacing: 0; font-size: 13px; }
  .kn { display: flex; align-items: center; }
'''
PARAMS = [('Center frequency', '1,000.00 Hz', False, 0.566, True), ('Gain', '6.00 dB', True, 0.5, False), ('Q factor', '0.7100', False, 0.34, True)]

def body(skin):
    parts = [f'<span class="sel fsel">Peaking filter <span class="chev"></span></span>']
    ksize = {'studio': 48, 'minimal': 46, 'soft': 54, 'rack': 56, 'matrix': 48}[skin]
    for label, value, bipolar, ratio, selector in PARAMS:
        k = knob(skin, bipolar, ratio, ksize)
        head = f'<span class="sel">{label} <span class="chev"></span></span>' if selector else f'<span class="cap">{label}</span>'
        parts.append(f'<div class="grp"><span class="kn">{k}</span><div class="prm">{head}<span class="vbox">{value}</span></div></div>')
    return f'<div class="pbody">{"".join(parts)}</div>'

frames = [frame(s, row(s, 'biquad', 1, 'Peaking', body(s), cursor=True)) for s in SKINS]
page('FilterCard', 'Rows', 900, 'The BiQuad row: shared header column, filter selector, three knob + parameter groups, five skins', CSS, frames)
