"""Knob instruments for the five skins. Importable: knob(skin, bipolar, ratio, size) returns an <svg>;
run as a script it writes project/components/Knob/preview.html (a gain knob at -3.0 dB and a
frequency knob at 40 %). ratio: bipolar -1..1 (share of the half range, negative = cut), unipolar 0..1."""
import math, os
root = os.path.dirname(os.path.abspath(__file__))

C = 37.0
def pt(r, deg):  # degrees clockwise from 12 o'clock
    a = math.radians(deg)
    return C + r * math.sin(a), C - r * math.cos(a)
def arc(r, a0, a1):
    x0, y0 = pt(r, a0); x1, y1 = pt(r, a1)
    sweep = 1 if a1 > a0 else 0
    large = 1 if abs(a1 - a0) > 180 else 0
    return f'M {x0:.2f} {y0:.2f} A {r} {r} 0 {large} {sweep} {x1:.2f} {y1:.2f}'
def f(v): return f'{v:.2f}'
def angle(bipolar, ratio):
    return 135.0 * ratio if bipolar else -135.0 + 270.0 * ratio

def studio(bipolar, ratio, text=''):
    r = 29; a = angle(bipolar, ratio)
    track = arc(r, -135, 135)
    val = arc(r, 0, a) if bipolar else arc(r, -135, a)
    end = pt(r, a)
    s = [f'<path d="{track}" fill="none" stroke="var(--border)" stroke-width="2"/>']
    if abs(a) > 0.5 or not bipolar:
        for w, o in ((13, .03), (9, .05), (5.5, .14), (2.5, 1)):  # four stacked strokes at rest (halo alpha 36)
            s.append(f'<path d="{val}" fill="none" stroke="var(--accent)" stroke-width="{w}" stroke-opacity="{o}" stroke-linecap="round"/>')
    if bipolar:  # the 0 dB anchor: outer 6 / inner 4 tick, band-colour bloom + text-ink core, drawn after the arc
        s.append(f'<line x1="{C}" y1="2" x2="{C}" y2="12" stroke="var(--accent)" stroke-width="3.5" stroke-opacity=".43"/>')
        s.append(f'<line x1="{C}" y1="2" x2="{C}" y2="12" stroke="var(--text)" stroke-width="1.4" stroke-opacity=".92"/>')
    s.append(f'<circle cx="{f(end[0])}" cy="{f(end[1])}" r="3" fill="var(--accent)" fill-opacity=".35"/>')
    s.append(f'<circle cx="{f(end[0])}" cy="{f(end[1])}" r="1.5" fill="var(--accent)"/>')
    if text:
        s.append(f'<text x="{C}" y="41" text-anchor="middle" font-family="var(--mono)" font-size="10.5" font-weight="600" fill="var(--text)" fill-opacity=".38">{text}</text>')
    return ''.join(s)

def minimal(bipolar, ratio, text=''):
    s = []
    for phi in range(-80, 81, 20):  # the drum seen from the side: hairlines packed denser toward the rims
        y = C + 31 * math.sin(math.radians(phi))
        s.append(f'<line x1="9" y1="{f(y)}" x2="53" y2="{f(y)}" stroke="var(--border)" stroke-width="1" shape-rendering="crispEdges"/>')
    if text:
        s.append('<rect x="9" y="27" width="44" height="20" fill="var(--bg)" stroke="var(--text)" stroke-width="1" shape-rendering="crispEdges"/>')
        s.append(f'<text x="31" y="41" text-anchor="middle" font-family="var(--mono)" font-size="11" font-weight="700" fill="var(--ink-bright)">{text}</text>')
    s.append(f'<line x1="4" y1="{C}" x2="9" y2="{C}" stroke="var(--muted)" stroke-width="1" shape-rendering="crispEdges"/>')
    s.append(f'<line x1="53" y1="{C}" x2="58" y2="{C}" stroke="var(--muted)" stroke-width="1" shape-rendering="crispEdges"/>')
    s.append('<line x1="66" y1="6" x2="66" y2="68" stroke="var(--border)" stroke-width="1" shape-rendering="crispEdges"/>')
    if bipolar:
        s.append(f'<line x1="63" y1="{C}" x2="69" y2="{C}" stroke="var(--muted)" stroke-width="1" shape-rendering="crispEdges"/>')
        y = C - ratio * 31
    else:
        y = 68 - ratio * 62
    s.append(f'<line x1="62" y1="{f(y)}" x2="70" y2="{f(y)}" stroke="var(--text)" stroke-width="1" shape-rendering="crispEdges"/>')
    return ''.join(s)

def soft(bipolar, ratio, text=''):
    r = 26; a = angle(bipolar, ratio)
    s = [f'<circle cx="{C}" cy="{C + 1.5}" r="36" fill="var(--surface)"/>',
         f'<circle cx="{C}" cy="{C}" r="35" fill="var(--card-hover)" stroke="var(--border)" stroke-width="1"/>']
    if bipolar:
        s.append(f'<path d="{arc(r, -135, 0)}" fill="none" stroke="color-mix(in srgb, var(--accent2) 20%, var(--card))" stroke-width="5" stroke-linecap="round"/>')
        s.append(f'<path d="{arc(r, 0, 135)}" fill="none" stroke="color-mix(in srgb, var(--accent) 20%, var(--card))" stroke-width="5" stroke-linecap="round"/>')
        ink = 'var(--accent2)' if ratio < 0 else 'var(--accent)'
        if abs(a) > 0.5:
            s.append(f'<path d="{arc(r, 0, a)}" fill="none" stroke="color-mix(in srgb, {ink} 75%, var(--card))" stroke-width="5" stroke-linecap="round"/>')
        s.append(f'<line x1="{C}" y1="{C - r - 4}" x2="{C}" y2="{C - r + 4}" stroke="var(--text)" stroke-width="2" stroke-linecap="round" stroke-opacity=".55"/>')
    else:
        s.append(f'<path d="{arc(r, -135, 135)}" fill="none" stroke="color-mix(in srgb, var(--accent) 20%, var(--card))" stroke-width="5" stroke-linecap="round"/>')
        s.append(f'<path d="{arc(r, -135, a)}" fill="none" stroke="color-mix(in srgb, var(--accent) 75%, var(--card))" stroke-width="5" stroke-linecap="round"/>')
        ink = 'var(--accent)'
    end = pt(r, a)
    s.append(f'<circle cx="{f(end[0])}" cy="{f(end[1])}" r="2.6" fill="{ink}"/>')
    return ''.join(s)

RACK_DEFS = '<defs><radialGradient id="rackBody" cx="38%" cy="32%" r="70%"><stop offset="0" style="stop-color:var(--card-hover)"/><stop offset="1" style="stop-color:color-mix(in srgb, var(--card) 55%, black)"/></radialGradient></defs>'

def rack(bipolar, ratio, text=''):
    s = []
    for i in range(11):  # the scale is printed on the panel, not the knob
        a = -135 + i * 27
        major = i in (0, 10)
        r0, r1 = (29, 35) if major else (30, 33.5)
        x0, y0 = pt(r0, a); x1, y1 = pt(r1, a)
        s.append(f'<line x1="{f(x0)}" y1="{f(y0)}" x2="{f(x1)}" y2="{f(y1)}" stroke="var({"--text" if major else "--muted"})" stroke-width="{1.5 if major else 1}"/>')
    if bipolar:  # the engraved 0 dB centre detent: the plate's longest, thickest mark, amber
        s.append(f'<line x1="{C}" y1="{C - 37}" x2="{C}" y2="{C - 27}" stroke="var(--accent)" stroke-width="3"/>')
        # the cut/boost glyphs (12 px bold) in the dead zone just past the scale ends, 2.5 px inside the scale radius
        for glyph, ratio_at in (('-', -0.07), ('+', 1.07)):
            gx, gy = pt(32.5, -135 + 270 * ratio_at)
            s.append(f'<text x="{f(gx)}" y="{f(gy)}" text-anchor="middle" dominant-baseline="central" font-family="var(--font)" font-size="12" font-weight="700" fill="var(--text)">{glyph}</text>')
    s.append(f'<circle cx="{C}" cy="{C + 1}" r="23" fill="rgba(0,0,0,.45)"/>')
    s.append(f'<circle cx="{C}" cy="{C}" r="22" fill="url(#rackBody)" stroke="#0a0c0e" stroke-width="1"/>')
    a = angle(bipolar, ratio)
    x1, y1 = pt(19, a); x0, y0 = pt(6, a)
    s.append(f'<line x1="{f(x0 + .7)}" y1="{f(y0 + .9)}" x2="{f(x1 + .7)}" y2="{f(y1 + .9)}" stroke="rgba(0,0,0,.6)" stroke-width="3" stroke-linecap="round"/>')
    s.append(f'<line x1="{f(x0)}" y1="{f(y0)}" x2="{f(x1)}" y2="{f(y1)}" stroke="var(--text)" stroke-width="1.6" stroke-linecap="round"/>')
    return ''.join(s)

def matrix(bipolar, ratio, text=''):
    r = 29
    n = 14 if bipolar else 15
    span = 270.0 / n; gap = 3.0
    s = []
    if bipolar:
        k = int(round(abs(ratio) * 7))
        lit = set(range(7 - k, 7)) if ratio < 0 else set(range(7, 7 + k))
    else:
        lit = set(range(int(round(ratio * 15))))
    for i in range(n):
        a0 = -135 + i * span + gap / 2; a1 = -135 + (i + 1) * span - gap / 2
        if i in lit:
            ink = 'var(--accent2)' if (bipolar and ratio < 0) else 'var(--accent)'
            s.append(f'<path d="{arc(r, a0, a1)}" fill="none" stroke="{ink}" stroke-width="3.5"/>')
        else:
            s.append(f'<path d="{arc(r, a0, a1)}" fill="none" stroke="var(--muted)" stroke-width="2.5" stroke-opacity=".31"/>')
    if bipolar:  # the detent tick outside the gap, 1px body ink
        s.append(f'<line x1="{C}" y1="{C - 36}" x2="{C}" y2="{C - 32}" stroke="var(--text)" stroke-width="1" shape-rendering="crispEdges"/>')
    s.append(f'<circle cx="{C}" cy="{C}" r="20" fill="var(--card)" stroke="var(--border)" stroke-width="1"/>')
    a = angle(bipolar, ratio)
    x1, y1 = pt(18, a); x0, y0 = pt(8, a)
    s.append(f'<line x1="{f(x0)}" y1="{f(y0)}" x2="{f(x1)}" y2="{f(y1)}" stroke="var(--text)" stroke-width="1.5"/>')
    return ''.join(s)

DRAW = {'studio': studio, 'minimal': minimal, 'soft': soft, 'rack': rack, 'matrix': matrix}

def knob(skin, bipolar, ratio, size=74, text=''):
    inner = DRAW[skin](bipolar, ratio, text)
    defs = RACK_DEFS if skin == 'rack' else ''
    return f'<svg viewBox="0 0 74 74" width="{size}" height="{size}" style="overflow:visible">{defs}{inner}</svg>'

if __name__ == '__main__':
    out = os.path.join(root, 'project', 'components', 'Knob', 'preview.html')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    CSS = '''  html, body { margin: 0; background: transparent; }
  .pair { display: flex; gap: 44px; align-items: flex-start; padding: 6px 4px 2px 12px; }
  .k { display: flex; flex-direction: column; align-items: center; gap: 6px; }
  .k svg { display: block; overflow: visible; }
  .lbl { font-size: 12px; font-weight: 600; color: var(--muted); }
  .skin-rack .lbl, .skin-matrix .lbl { text-transform: uppercase; letter-spacing: 1px; font-size: 10.67px; }
  .skin-rack .lbl { font-weight: 700; }
  .skin-minimal .lbl { font-family: var(--mono); text-transform: uppercase; letter-spacing: 1px; }
  .soft-badge { font-family: var(--mono); font-size: 12px; font-weight: 600; color: var(--text); background: var(--sunken); border-radius: 999px; padding: 2px 10px; }
  .rack-row { display: flex; align-items: center; gap: 12px; }
  .lcd { font-family: var(--mono); font-size: 13.33px; font-weight: 700; color: var(--accent2); background: var(--graph); border: 1px solid #0a0c0e;
    border-radius: 2px; padding: 3px 8px; box-shadow: inset 0 1px 2px rgba(0,0,0,.8), 0 1px 0 rgba(255,255,255,.06); text-shadow: 0 0 4px color-mix(in srgb, var(--accent2) 60%, transparent); }
  [data-theme="light"] .lcd { background: #11150f; color: #3ed68e; }
  .cell { height: 16px; min-width: 52px; display: inline-flex; align-items: center; justify-content: center; font-family: var(--mono); font-size: 11px; font-weight: 700;
    color: var(--text); background: var(--graph); border: 1px solid var(--border); }
  .skin-soft .k { gap: 8px; }
'''
    def frame(skin, cells): return f'<div class="skin skin-{skin}"><span class="skin-caption">{skin}</span><div class="pair">{"".join(cells)}</div></div>'
    def cell(label, body): return f'<div class="k"><div class="lbl">{label}</div>{body}</div>'
    G, F = -0.25, 0.40  # -3 dB of +-12, 40 % of travel
    html = ['<!-- @dsCard group="Controls" height=790 width=960 subtitle="Bipolar gain and unipolar frequency knobs, five instruments" -->',
            '<!doctype html>', '<html lang="ko">', '<head>', '<meta charset="utf-8">', '<title>Knob — preview</title>', '<style>', CSS, '</style>', '</head>', '<body>', '<div class="ds-stack">',
            frame('studio', [cell('Gain', knob('studio', True, G, text='-3.0 dB')), cell('Freq', knob('studio', False, F, text='1.00 kHz'))]),
            frame('minimal', [cell('Gain', knob('minimal', True, G, text='-03.0')), cell('Freq', knob('minimal', False, F, text='01000'))]),
            frame('soft', [cell('Gain', knob('soft', True, G) + '<span class="soft-badge">-3.0 dB</span>'), cell('Frequency', knob('soft', False, F) + '<span class="soft-badge">1000 Hz</span>')]),
            frame('rack', [cell('Gain', '<div class="rack-row">' + knob('rack', True, G) + '<span class="lcd">-3.0</span></div>'), cell('Freq', '<div class="rack-row">' + knob('rack', False, F) + '<span class="lcd">1000</span></div>')]),
            frame('matrix', [cell('Gain', knob('matrix', True, G) + '<span class="cell">-3.0 dB</span>'), cell('Freq', knob('matrix', False, F) + '<span class="cell">1000 Hz</span>')]),
            '</div>', '</body>', '</html>', '']
    open(out, 'w', encoding='utf-8').write('\n'.join(html))
    print('wrote', out, len('\n'.join(html)), 'bytes')
