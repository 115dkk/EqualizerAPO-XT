"""Generates project/components/AnalysisGraph/preview.html: the analysis dock's response graph in five skins at the
gallery's 940x220 (graph_normal). The response clips above 0 dB, so every skin also shows its clip notation."""
import math, os
root = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(root, 'project', 'components', 'AnalysisGraph', 'preview.html')
os.makedirs(os.path.dirname(out), exist_ok=True)

W, H = 940, 220
L, R, T, B = 40, 920, 26, 190
MID = (T + B) / 2
def fx(f): return L + (math.log10(f) - math.log10(20)) / 3.0 * (R - L)
def fy(g): return MID - g / 12.0 * (B - T) / 2
# the gallery's response: a +6 dB hump around 45 Hz, a dip at 300 Hz and 2.5 kHz, a +6 dB hump at 10 kHz (a piecewise sketch)
KNOTS = [(20, 0), (45, 5.5), (170, 0), (300, -3.5), (900, 0.5), (1000, 0.8), (1100, 0), (2500, -5), (5000, 0), (10000, 6.5), (20000, -2)]
def gain(f):
    lf = math.log10(f)
    for (f0, g0), (f1, g1) in zip(KNOTS, KNOTS[1:]):
        if f0 <= f <= f1:
            t = (lf - math.log10(f0)) / (math.log10(f1) - math.log10(f0))
            return g0 + (g1 - g0) * t
    return 0.0
pts = [(fx(20 * 10 ** (3.0 * i / 240)), fy(gain(20 * 10 ** (3.0 * i / 240)))) for i in range(241)]
TRACE = 'M ' + ' L '.join(f'{x:.1f} {y:.1f}' for x, y in pts)
FILL = f'M {pts[0][0]:.1f} {MID:.1f} L ' + ' L '.join(f'{x:.1f} {y:.1f}' for x, y in pts) + f' L {pts[-1][0]:.1f} {MID:.1f} Z'
FREQ_LABELS = [(20, '20'), (50, '50'), (100, '100'), (200, '200'), (500, '500'), (1000, '1k'), (2000, '2k'), (5000, '5k'), (10000, '10k'), (20000, '20k')]
DB_LABELS = [(12, '+12'), (6, '+6'), (0, '0'), (-6, '-6'), (-12, '-12')]
FREQ_MAJ = [100, 1000, 10000]; FREQ_MIN = [20, 50, 200, 500, 2000, 5000, 20000]
DB_MAJ = [-12, -6, 6, 12]; DB_MIN = [-9, -3, 3, 9]

def lines(freqs, dbs, stroke, opacity=1, crisp=True, width=1):
    s = []; cr = ' shape-rendering="crispEdges"' if crisp else ''
    for f in freqs:
        x = round(fx(f)) + 0.5 if crisp else fx(f)
        s.append(f'<line x1="{x}" y1="{T}" x2="{x}" y2="{B}" stroke="{stroke}" stroke-opacity="{opacity}" stroke-width="{width}"{cr}/>')
    for g in dbs:
        y = round(fy(g)) + 0.5 if crisp else fy(g)
        s.append(f'<line x1="{L}" y1="{y}" x2="{R}" y2="{y}" stroke="{stroke}" stroke-opacity="{opacity}" stroke-width="{width}"{cr}/>')
    return ''.join(s)

def labels(font, size, fill, weight=500, both=False, y_above=False, extra='', dbs=DB_LABELS):
    s = []
    for f, t in FREQ_LABELS:
        s.append(f'<text x="{fx(f):.1f}" y="{B + 13}" text-anchor="middle" font-family="{font}" font-size="{size}" font-weight="{weight}" fill="{fill}"{extra}>{t}</text>')
    for g, t in dbs:
        y = fy(g) - 3 if y_above else fy(g) + 3.5
        s.append(f'<text x="{L - 5}" y="{y:.1f}" text-anchor="end" font-family="{font}" font-size="{size}" font-weight="{weight}" fill="{fill}"{extra}>{t}</text>')
        if both: s.append(f'<text x="{R + 5}" y="{y:.1f}" font-family="{font}" font-size="{size}" font-weight="{weight}" fill="{fill}"{extra}>{t}</text>')
    return ''.join(s)

def clip_defs(pfx):
    return (f'<clipPath id="{pfx}-up"><rect x="{L}" y="{T}" width="{R - L}" height="{MID - T}"/></clipPath>'
            f'<clipPath id="{pfx}-dn"><rect x="{L}" y="{MID}" width="{R - L}" height="{B - MID}"/></clipPath>')
CAPTION = 'All - 48000 Hz'

def studio():
    s = [f'<defs>{clip_defs("st")}<linearGradient id="st-up" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--danger)" stop-opacity=".22"/><stop offset="1" stop-color="var(--danger)" stop-opacity="0"/></linearGradient>'
         f'<linearGradient id="st-dn" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--accent)" stop-opacity="0"/><stop offset="1" stop-color="var(--accent)" stop-opacity=".16"/></linearGradient>'
         f'<linearGradient id="st-wash" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--danger)" stop-opacity=".16"/><stop offset="1" stop-color="var(--danger)" stop-opacity="0"/></linearGradient></defs>',
         f'<rect x="{L}" y="{T}" width="{R - L}" height="{B - T}" rx="10" fill="var(--graph)"/>',
         f'<rect x="{L}" y="{T}" width="{R - L}" height="{MID - T}" fill="url(#st-wash)"/>',  # the glass above 0 dB warms in danger while the response can clip
         lines(FREQ_MIN, DB_MIN, 'var(--grid-minor)', .33), lines(FREQ_MAJ, DB_MAJ, 'var(--grid-minor)', .6),
         f'<path d="{FILL}" fill="url(#st-up)" clip-path="url(#st-up)"/>', f'<path d="{FILL}" fill="url(#st-dn)" clip-path="url(#st-dn)"/>']
    y0 = round(fy(0)) + .5
    for w, o in ((7, .05), (4, .16), (2, .45), (1, 1)):
        s.append(f'<line x1="{L + 8}" y1="{y0}" x2="{R - 8}" y2="{y0}" stroke="var(--text)" stroke-width="{w}" stroke-opacity="{o}"/>')
    for w, o in ((13, .03), (9, .06), (5.5, .16), (2.5, 1)):  # the trace: accent below 0 dB, the excess ignites in the danger stroke ladder
        s.append(f'<path d="{TRACE}" fill="none" stroke="var(--accent)" stroke-width="{w}" stroke-opacity="{o}" stroke-linejoin="round" clip-path="url(#st-dn)"/>')
        s.append(f'<path d="{TRACE}" fill="none" stroke="var(--danger)" stroke-width="{w}" stroke-opacity="{o}" stroke-linejoin="round" clip-path="url(#st-up)"/>')
    s.append(labels('var(--mono)', 10, 'var(--muted)'))
    s.append(f'<rect x="{R - 58}" y="{T + 8}" width="46" height="18" rx="6" fill="var(--danger)" fill-opacity=".15" stroke="var(--danger)" stroke-opacity=".6"/>'
             f'<text x="{R - 35}" y="{T + 21}" text-anchor="middle" font-family="var(--mono)" font-size="10" font-weight="700" letter-spacing="1" fill="var(--danger)">CLIP</text>')
    s.append(f'<text x="{(L + R) / 2}" y="{H - 4}" text-anchor="middle" font-family="var(--mono)" font-size="9.5" fill="var(--muted)">{CAPTION}</text>')
    return ''.join(s)

def minimal():
    s = [f'<defs>{clip_defs("mn")}</defs><rect x="{L + .5}" y="{T + .5}" width="{R - L - 1}" height="{B - T - 1}" fill="var(--graph)" stroke="var(--border)" shape-rendering="crispEdges"/>',
         lines(FREQ_MIN + FREQ_MAJ, DB_MIN + DB_MAJ, 'var(--grid-minor)', .8)]
    y0 = round(fy(0)) + .5
    # the clip is a reverse-video error block: the excess area filled in danger sunk to the sheet's register, the trace inverted through it
    s.append(f'<path d="{FILL}" fill="color-mix(in srgb, var(--danger) 55%, var(--graph))" clip-path="url(#mn-up)"/>')
    s.append(f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--text)" stroke-width="1" shape-rendering="crispEdges"/>')
    s.append(f'<path d="{TRACE}" fill="none" stroke="var(--text)" stroke-width="1"/>')
    s.append(labels('var(--mono)', 10, 'var(--muted)', both=True))
    s.append(f'<rect x="{L - 30}" y="{fy(0) - 6}" width="26" height="12" fill="var(--bg)"/><text x="{L - 5}" y="{fy(0) + 3.5}" text-anchor="end" font-family="var(--mono)" font-size="10" fill="var(--text)">0</text>')
    s.append(f'<text x="{L}" y="{T - 8}" font-family="var(--mono)" font-size="10" font-weight="700" letter-spacing="1" fill="var(--muted)">RESPONSE</text>')
    s.append(f'<text x="{L + 86}" y="{T - 8}" font-family="var(--mono)" font-size="10" font-weight="700" letter-spacing="1" fill="var(--danger)">!! OVER 0 DB</text>')
    s.append(f'<text x="{L}" y="{H - 4}" font-family="var(--mono)" font-size="9.5" fill="var(--muted)">{CAPTION}</text>')
    return ''.join(s)

def soft():
    s = [f'<defs>{clip_defs("so")}</defs>', f'<rect x="{L}" y="{T}" width="{R - L}" height="{B - T}" rx="14" fill="var(--sunken)" stroke="var(--border)"/>']
    s.append(lines(FREQ_MAJ, [], 'var(--border)', .7, crisp=False))
    s.append(f'<path d="{FILL}" fill="var(--warning)" clip-path="url(#so-up)"/>')  # the boost hill warms to warning while the setting can clip
    s.append(f'<path d="{FILL}" fill="var(--accent)" clip-path="url(#so-dn)"/>')
    s.append(f'<path d="{TRACE}" fill="none" stroke="color-mix(in srgb, var(--warning) 70%, var(--on-accent))" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" clip-path="url(#so-up)"/>')
    s.append(f'<path d="{TRACE}" fill="none" stroke="color-mix(in srgb, var(--accent) 70%, var(--on-accent))" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" clip-path="url(#so-dn)"/>')
    s.append(labels('var(--font)', 10.5, 'var(--muted)', y_above=True, dbs=[(12, '+12'), (6, '+6'), (0, '0'), (-6, '-6'), (-12, '-12')]))
    s.append(f'<rect x="{L + 10}" y="{T + 8}" width="66" height="20" rx="10" fill="var(--warning)"/><text x="{L + 43}" y="{T + 22}" text-anchor="middle" font-family="var(--font)" font-size="10.5" font-weight="600" fill="var(--on-accent)">Over 0 dB</text>')
    s.append(f'<text x="{L + 84}" y="{T + 22}" font-family="var(--font)" font-size="10.5" font-weight="600" fill="var(--muted)">Sound may distort - keep it below 0 dB</text>')
    s.append(f'<text x="{(L + R) / 2}" y="{H - 4}" text-anchor="middle" font-family="var(--font)" font-size="10.5" fill="var(--muted)">{CAPTION}</text>')
    return ''.join(s)

def rack():
    s = [f'<defs>{clip_defs("rk")}<linearGradient id="rk-glow" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--scope)" stop-opacity=".18"/><stop offset="1" stop-color="var(--scope)" stop-opacity=".02"/></linearGradient>'
         f'<linearGradient id="rk-over" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="var(--danger)" stop-opacity=".22"/><stop offset="1" stop-color="var(--danger)" stop-opacity=".04"/></linearGradient></defs>',
         f'<rect x="{L - 10}" y="{T - 8}" width="{R - L + 20}" height="{B - T + 16}" rx="3" fill="var(--card)" stroke="var(--seam)"/>',
         f'<rect x="{L}" y="{T}" width="{R - L}" height="{B - T}" fill="var(--glass)" stroke="#0a0c0e"/>',
         f'<rect x="{L}" y="{T}" width="{R - L}" height="{MID - T}" fill="url(#rk-over)"/>',  # the OVER zone: the band above the 0 dB axis heats in danger red
         f'<rect x="{L}" y="{T}" width="{R - L}" height="4" fill="rgba(0,0,0,.55)"/>', f'<rect x="{L}" y="{B - 1}" width="{R - L}" height="1" fill="rgba(255,255,255,.08)"/>']
    s.append(lines(FREQ_MIN + FREQ_MAJ, [-12, -9, -6, -3], 'var(--rack-scope-grid)', 1))
    s.append(lines([], [3, 6, 9, 12], 'var(--danger)', .35))
    for f in FREQ_MIN + FREQ_MAJ:
        x = round(fx(f)) + .5
        s.append(f'<line x1="{x}" y1="{T}" x2="{x}" y2="{MID}" stroke="var(--danger)" stroke-opacity=".28" shape-rendering="crispEdges"/>')
    y0 = round(fy(0)) + .5
    s.append(f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--scope)" stroke-opacity=".55" stroke-width="1" shape-rendering="crispEdges"/>')
    s.append(f'<path d="{FILL}" fill="url(#rk-glow)" clip-path="url(#rk-dn)"/>')
    for w, o in ((9, .08), (5, .22), (2.5, .6), (1.4, 1)):
        s.append(f'<path d="{TRACE}" fill="none" stroke="var(--scope)" stroke-width="{w}" stroke-opacity="{o}" stroke-linejoin="round" clip-path="url(#rk-dn)"/>')
        s.append(f'<path d="{TRACE}" fill="none" stroke="var(--danger)" stroke-width="{w}" stroke-opacity="{o}" stroke-linejoin="round" clip-path="url(#rk-up)"/>')
    s.append(f'<path d="{TRACE}" fill="none" stroke="#fff" stroke-width=".8" stroke-opacity=".6" clip-path="url(#rk-up)"/>')  # the white-hot core of the overdriven beam
    s.append(labels('var(--mono)', 9.5, 'var(--accent2)', weight=700, extra=' fill-opacity=".85"', dbs=[(0, '0'), (-6, '-6'), (-12, '-12')]))
    for g, t in ((12, '+12'), (6, '+6')):
        s.append(f'<text x="{L - 5}" y="{fy(g) + 3.5:.1f}" text-anchor="end" font-family="var(--mono)" font-size="9.5" font-weight="700" fill="var(--danger)">{t}</text>')
    s.append(f'<text x="{L}" y="{H - 3}" font-family="var(--font)" font-size="9" font-weight="700" letter-spacing="2" fill="var(--muted)">SPECTRUM MONITOR</text>')
    s.append(f'<text x="{(L + R) / 2}" y="{H - 3}" text-anchor="middle" font-family="var(--font)" font-size="10" fill="var(--text)">{CAPTION}</text>')
    s.append(f'<text x="{R - 14}" y="{H - 3}" text-anchor="end" font-family="var(--font)" font-size="9" font-weight="700" letter-spacing="2" fill="var(--danger)">OVER</text>')
    s.append(f'<circle cx="{R - 4}" cy="{H - 6}" r="3.5" fill="var(--danger)" stroke="#0a0c0e" stroke-width="1.5"/><circle cx="{R - 5}" cy="{H - 7}" r="1" fill="#fff" fill-opacity=".5"/>')
    return ''.join(s)

def matrix():
    s = [f'<defs>{clip_defs("mx")}<pattern id="mx-hz" width="12" height="12" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="12" stroke="var(--warning)" stroke-opacity=".28" stroke-width="1"/></pattern>'
         f'<pattern id="mx-hz2" width="5" height="5" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="5" stroke="var(--warning)" stroke-width="1"/></pattern></defs>',
         f'<rect x="{L + .5}" y="{T + .5}" width="{R - L - 1}" height="{B - T - 1}" fill="var(--graph)" stroke="var(--border)" shape-rendering="crispEdges"/>',
         f'<rect x="{L}" y="{T}" width="{R - L}" height="{MID - T}" fill="url(#mx-hz)"/>',  # the hazard band: thin amber diagonals at the board's half pitch
         f'<path d="{FILL}" fill="url(#mx-hz2)" clip-path="url(#mx-up)"/>',  # where the trace actually exceeds the bus the hatching densifies
         lines(FREQ_MIN, DB_MIN, 'var(--grid-minor)', 1), lines(FREQ_MAJ, DB_MAJ, 'var(--muted)', .35)]
    y0 = round(fy(0)) + .5
    s.append(f'<line x1="{L}" y1="{y0}" x2="{R}" y2="{y0}" stroke="var(--text)" stroke-width="1" shape-rendering="crispEdges"/>')
    s.append(f'<path d="{TRACE}" fill="none" stroke="var(--accent)" stroke-width="4" stroke-opacity=".18" stroke-linejoin="round"/>')
    s.append(f'<path d="{TRACE}" fill="none" stroke="var(--accent)" stroke-width="1" stroke-linejoin="round"/>')
    for f, t in FREQ_LABELS:
        x = fx(f)
        s.append(f'<rect x="{x - 12:.1f}" y="{B + 3}" width="24" height="12" fill="var(--bg)"/><text x="{x:.1f}" y="{B + 12.5}" text-anchor="middle" font-family="var(--mono)" font-size="9.5" font-weight="600" fill="var(--muted)">{t}</text>')
    for g, t in DB_LABELS:
        y = fy(g)
        s.append(f'<rect x="{L - 30}" y="{y - 6:.1f}" width="26" height="12" fill="var(--bg)"/><text x="{L - 5}" y="{y + 3.5:.1f}" text-anchor="end" font-family="var(--mono)" font-size="9.5" font-weight="600" fill="var(--muted)">{t}</text>')
    px, py = fx(10000), fy(6.5)
    s.append(f'<line x1="{px:.1f}" y1="{py - 4:.1f}" x2="{px:.1f}" y2="{py - 16:.1f}" stroke="var(--warning)" shape-rendering="crispEdges"/>'
             f'<rect x="{px - 22:.1f}" y="{py - 30:.1f}" width="44" height="14" fill="var(--graph)" stroke="var(--warning)" shape-rendering="crispEdges"/>'
             f'<text x="{px:.1f}" y="{py - 19.5:.1f}" text-anchor="middle" font-family="var(--mono)" font-size="9.5" font-weight="700" letter-spacing="1" fill="var(--warning)">OVER</text>')
    s.append(f'<text x="{L}" y="{T - 8}" font-family="var(--mono)" font-size="10" font-weight="700" letter-spacing="2" fill="var(--muted)">RESPONSE</text>')
    s.append(f'<rect x="{L}" y="{H - 16}" width="{R - L}" height="15" fill="var(--graph)" stroke="var(--border)" shape-rendering="crispEdges"/>')
    s.append(f'<text x="{L + 6}" y="{H - 5}" font-family="var(--mono)" font-size="10" font-weight="700" fill="var(--accent)">&gt;</text>')
    s.append(f'<text x="{L + 18}" y="{H - 5}" font-family="var(--mono)" font-size="10" fill="var(--muted)">{CAPTION}</text>')
    s.append(f'<text x="{R - 6}" y="{H - 5}" text-anchor="end" font-family="var(--mono)" font-size="10" font-weight="700" fill="var(--text)">+12 / -12 DB</text>')
    return ''.join(s)

def frame(skin, inner):
    return f'<div class="skin skin-{skin}"><span class="skin-caption">{skin}</span><svg viewBox="0 0 {W} {H}" width="{W}" height="{H}">{inner}</svg></div>'

html = ['<!-- @dsCard group="Instruments" height=1260 width=960 subtitle="Analysis dock response graph at the gallery size (940x220), clipping above 0 dB, five instruments" -->',
'<!doctype html>', '<html lang="ko">', '<head>', '<meta charset="utf-8">', '<title>AnalysisGraph — preview</title>', '<style>',
'  html, body { margin: 0; background: transparent; }\n  .skin svg { display: block; width: 100%; height: auto; overflow: visible; }\n  .skin { padding: 14px 10px 10px; }',
'</style>', '</head>', '<body>', '<div class="ds-stack">',
frame('studio', studio()), frame('minimal', minimal()), frame('soft', soft()), frame('rack', rack()), frame('matrix', matrix()),
'</div>', '</body>', '</html>', '']
open(out, 'w', encoding='utf-8').write('\n'.join(html))
print('wrote', out, len('\n'.join(html)), 'bytes')
