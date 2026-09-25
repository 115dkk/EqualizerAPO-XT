"""GraphicEQCard: the GraphicEQ row (graphiceq_normal: 948x361..372): toolbar line, the response plot with five nodes,
the readout strip. The plot is painted per skin (paintGraphicEqPlot)."""
import math
from rowlib import *

W, H = 900, 200
L, R, T, B = 44, 884, 12, 170
NODES = [(25, -4.5), (100, -2.5), (1000, 0.0), (8000, 2.5), (20000, 0.5)]
def fx(f): return L + (math.log10(f) - math.log10(20)) / 3.0 * (R - L)
def fy(g): return (T + B) / 2 - g / 20.0 * (B - T) / 2
def polyline():  # the gallery's plot joins the band nodes with straight segments in log-x
    pts = [(fx(20), fy(NODES[0][1]))] + [(fx(f), fy(g)) for f, g in NODES]
    return pts
PTS = polyline()
TRACE = 'M ' + ' L '.join(f'{x:.1f} {y:.1f}' for x, y in PTS)
FILL = f'M {PTS[0][0]:.1f} {fy(0):.1f} L ' + ' L '.join(f'{x:.1f} {y:.1f}' for x, y in PTS) + f' L {PTS[-1][0]:.1f} {fy(0):.1f} Z'
FREQ = [(20, '20'), (50, '50'), (100, '100'), (200, '200'), (500, '500'), (1000, '1k'), (2000, '2k'), (5000, '5k'), (10000, '10k'), (20000, '20k')]
DB = [(20, '+20'), (15, '+15'), (10, '+10'), (5, '+5'), (0, '0'), (-5, '-5'), (-10, '-10'), (-15, '-15'), (-20, '-20')]

def grid(stroke, opacity, crisp=True, majors_only=False):
    s = []; cr = ' shape-rendering="crispEdges"' if crisp else ''
    for f, _ in FREQ:
        if majors_only and f not in (100, 1000, 10000): continue
        x = round(fx(f)) + .5
        s.append(f'<line x1="{x}" y1="{T}" x2="{x}" y2="{B}" stroke="{stroke}" stroke-opacity="{opacity}"{cr}/>')
    for g, _ in DB:
        if g == 0 or (majors_only and g % 10): continue
        y = round(fy(g)) + .5
        s.append(f'<line x1="{L}" y1="{y}" x2="{R}" y2="{y}" stroke="{stroke}" stroke-opacity="{opacity}"{cr}/>')
    return ''.join(s)

def labels(font, size, fill, weight=500, majors_only=False):
    # majors_only: soft prints only the decade frequencies and the 10 dB steps, like its majors-only grid
    s = [f'<text x="{fx(f):.1f}" y="{B + 13}" text-anchor="middle" font-family="{font}" font-size="{size}" font-weight="{weight}" fill="{fill}">{t}</text>'
         for f, t in FREQ if not majors_only or f in (20, 100, 1000, 10000, 20000)]
    s += [f'<text x="{L - 5}" y="{fy(g) + 3.5:.1f}" text-anchor="end" font-family="{font}" font-size="{size}" font-weight="{weight}" fill="{fill}">{t}</text>'
          for g, t in DB if not majors_only or g % 10 == 0]
    return ''.join(s)

def nodes(kind):
    s = []
    for i, (f, g) in enumerate(NODES):
        x, y = fx(f), fy(g)
        if kind == 'studio':
            s.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.5" fill="var(--accent)" fill-opacity=".35"/><circle cx="{x:.1f}" cy="{y:.1f}" r="1.8" fill="var(--accent)"/>')
        elif kind == 'minimal':
            s.append(f'<rect x="{x - 3:.1f}" y="{y - 3:.1f}" width="6" height="6" fill="var(--graph)" stroke="var(--text)" shape-rendering="crispEdges"/>')
        elif kind == 'soft':
            ink = 'var(--accent)' if g >= 0 else 'var(--accent2)'
            s.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="5" fill="var(--card)" stroke="{ink}" stroke-width="2"/>')
        elif kind == 'rack':
            s.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="var(--accent2)" fill-opacity=".9"/><circle cx="{x:.1f}" cy="{y:.1f}" r="5" fill="var(--accent2)" fill-opacity=".18"/>')
        elif kind == 'matrix':
            s.append(f'<rect x="{x - 3:.1f}" y="{y - 3:.1f}" width="6" height="6" fill="var(--graph)" stroke="var(--muted)" shape-rendering="crispEdges"/>')
            if i == 0: s.append(f'<text x="{x + 9:.1f}" y="{y - 6:.1f}" font-family="var(--mono)" font-size="9" fill="var(--muted)">1</text>')
    return ''.join(s)

def plot(skin):
    y0 = round(fy(0)) + .5
    if skin == 'studio':
        inner = (f'<defs><linearGradient id="gq-f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--accent)" stop-opacity=".18"/><stop offset="1" stop-color="var(--accent)" stop-opacity="0"/></linearGradient></defs>'
                 f'<rect x="{L}" y="{T}" width="{R - L}" height="{B - T}" rx="8" fill="var(--graph)"/><rect x="{L}" y="{T}" width="{R - L}" height="1" fill="rgba(0,0,0,.6)"/>'
                 + grid('var(--grid-minor)', .5) + f'<path d="{FILL}" fill="url(#gq-f)"/>'
                 f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--accent)" stroke-width="3" stroke-opacity=".35"/><line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--text)" stroke-width="1" shape-rendering="crispEdges"/>')
        for w, o in ((9, .04), (5.5, .1), (3, .3), (1.6, 1)):
            inner += f'<path d="{TRACE}" fill="none" stroke="var(--accent)" stroke-width="{w}" stroke-opacity="{o}" stroke-linejoin="round"/>'
        inner += nodes('studio') + labels('var(--mono)', 9.5, 'var(--muted)')
    elif skin == 'minimal':
        inner = (f'<rect x="{L + .5}" y="{T + .5}" width="{R - L - 1}" height="{B - T - 1}" fill="var(--graph)" stroke="var(--border)" shape-rendering="crispEdges"/>'
                 + grid('var(--grid-minor)', .9) + f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--text)" shape-rendering="crispEdges"/>'
                 f'<path d="{TRACE}" fill="none" stroke="var(--text)" stroke-width="1"/>' + nodes('minimal') + labels('var(--mono)', 9.5, 'var(--muted)'))
    elif skin == 'soft':
        inner = (f'<rect x="{L}" y="{T}" width="{R - L}" height="{B - T}" rx="14" fill="var(--sunken)" stroke="var(--border)"/>' + grid('var(--border)', .7, False, True)
                 + f'<line x1="{L + 10}" y1="{fy(0):.1f}" x2="{R - 10}" y2="{fy(0):.1f}" stroke="var(--text)" stroke-width="2" stroke-linecap="round" stroke-opacity=".6"/>'
                 f'<path d="{FILL}" fill="var(--accent2)" fill-opacity=".18"/>'
                 f'<path d="{TRACE}" fill="none" stroke="color-mix(in srgb, var(--accent2) 75%, var(--card))" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>'
                 + nodes('soft') + labels('var(--font)', 10, 'var(--muted)', majors_only=True))
    elif skin == 'rack':
        inner = (f'<rect x="{L - 8}" y="{T - 6}" width="{R - L + 16}" height="{B - T + 12}" rx="3" fill="var(--card)" stroke="var(--seam)"/>'
                 f'<rect x="{L}" y="{T}" width="{R - L}" height="{B - T}" fill="var(--glass)" stroke="#0a0c0e"/><rect x="{L}" y="{T}" width="{R - L}" height="4" fill="rgba(0,0,0,.55)"/>'
                 + grid('var(--rack-scope-grid)', 1) + f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--accent2)" stroke-opacity=".6" shape-rendering="crispEdges"/>'
                 f'<path d="{FILL}" fill="var(--accent2)" fill-opacity=".08"/>')
        for w, o in ((7, .08), (3.5, .25), (1.5, 1)):
            inner += f'<path d="{TRACE}" fill="none" stroke="var(--accent2)" stroke-width="{w}" stroke-opacity="{o}" stroke-linejoin="round"/>'
        inner += nodes('rack') + labels('var(--mono)', 9, 'var(--accent2)', 700)
    else:
        inner = (f'<rect x="{L + .5}" y="{T + .5}" width="{R - L - 1}" height="{B - T - 1}" fill="var(--graph)" stroke="var(--border)" shape-rendering="crispEdges"/>'
                 + grid('var(--grid-minor)', 1) + f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--text)" shape-rendering="crispEdges"/>'
                 f'<path d="{TRACE}" fill="none" stroke="var(--accent)" stroke-width="2" stroke-linejoin="round"/>' + nodes('matrix') + labels('var(--mono)', 9.5, 'var(--muted)', 600))
    return f'<svg viewBox="0 0 {W} {H}" width="100%" height="auto" style="display:block;overflow:visible">{inner}</svg>'

ICONS = {'open': PICT['folder'], 'save': PICT['save'],
         'invert': '<path d="M8 4v16"/><path d="M4.5 7.5L8 4l3.5 3.5"/><path d="M16 20V4"/><path d="M12.5 16.5L16 20l3.5-3.5"/>',
         'normalize': '<path d="M5 17v-6"/><path d="M12 17V5"/><path d="M19 17v-9"/><path d="M3 20h18"/>',
         'reset': '<path d="M4 12a8 8 0 1 0 2.4-5.7"/><path d="M4 4v5h5"/>'}
def ico(name): return f'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">{ICONS[name]}</svg>'

CSS = '''
  .gtool { display: flex; align-items: center; gap: 6px; height: 34px; margin-bottom: 6px; }
  .gtool .sel { min-width: 110px; height: 30px; }
  .gtool .ibtn { width: 34px; height: 30px; }
  .gplot { margin: 0 0 4px; }
  .gread { display: flex; align-items: center; gap: 12px; height: 34px; }
  .gread .band { color: var(--muted); font-size: 12.5px; }
  .gread .lab { margin-left: auto; }
  .gread .vbox { min-width: 110px; height: 30px; }
  .skin-minimal .gtool .ibtn svg { display: block; }
  .skin-minimal .gtool .ibtn { border-bottom: 0; color: var(--text); padding: 0; }
  .skin-minimal .gread .band, .skin-minimal .gread .lab { font-family: var(--mono); text-transform: uppercase; letter-spacing: 1px; font-size: 11px; }
  .skin-minimal .gread .vbox { background: transparent; border: 0; color: var(--ink-bright); min-width: 0; }
  .skin-matrix .gread .band { font-family: var(--mono); text-transform: uppercase; letter-spacing: 1px; font-size: 11px; }
'''

def body(skin):
    tool = (f'<div class="gtool"><span class="sel">variable <span class="chev"></span></span>' + ''.join(f'<span class="ibtn">{ico(n)}</span>' for n in ('open', 'save', 'invert', 'normalize', 'reset')) + '</div>')
    read = (f'<div class="gread"><span class="band">Band 1 / 5</span><span class="cap lab">Freq.</span><span class="vbox">25.0 Hz</span><span class="cap">Gain</span><span class="vbox">-4.5 dB</span></div>')
    return tool + f'<div class="gplot">{plot(skin)}</div>' + read

frames = [frame(s, row(s, 'geq', 20, 'Graphic EQ', body(s), chans=('L', 'R'))) for s in SKINS]
page('GraphicEQCard', 'Rows', 1840, 'The GraphicEQ row: toolbar line, the painted response plot with band nodes, the readout strip, five skins', CSS, frames)
