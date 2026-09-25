"""Shared builders for the card-row previews: the header contract, pictograms, frames and the page shell.
Every row preview imports this so the five skins' header chrome is drawn once (bundle.css .crow grammar)."""
import os
ROOT = os.path.dirname(os.path.abspath(__file__))
SKINS = ('studio', 'minimal', 'soft', 'rack', 'matrix')

PICT = {
    'biquad': '<path d="M3 15.5H6.5C9 15.5 9.5 6.5 12 6.5C14.5 6.5 15 15.5 17.5 15.5H21"/>',
    'include': '<path d="M13.5 3.5H7A1.5 1.5 0 0 0 5.5 5v14A1.5 1.5 0 0 0 7 20.5h10A1.5 1.5 0 0 0 18.5 19V8.5z"/><path d="M13.5 3.5V8.5H18.5"/><path d="M8.8 12.5h6.4"/><path d="M8.8 15.5h6.4"/>',
    'vst': '<rect x="6.5" y="6.5" width="11" height="11" rx="2"/><path d="M9.5 3.5v3M14.5 3.5v3M9.5 17.5v3M14.5 17.5v3"/><path d="M3.5 9.5h3M3.5 14.5h3M17.5 9.5h3M17.5 14.5h3"/>',
    'copy': '<path d="M3 7.5C10 7.5 14 16.5 21 16.5"/><path d="M3 16.5C10 16.5 14 7.5 21 7.5"/><path d="M18.3 5.6L21 7.5l-2.8 1.7"/><path d="M18.3 14.6L21 16.5l-2.8 1.7"/>',
    'comment': '<path d="M6 5.5h12a1.5 1.5 0 0 1 1.5 1.5v7.5a1.5 1.5 0 0 1-1.5 1.5h-6.5L7 19.5v-3.5H6a1.5 1.5 0 0 1-1.5-1.5V7A1.5 1.5 0 0 1 6 5.5z"/>',
    'if': '<path d="M12 6v4"/><path d="M12 10l4.8 4-4.8 4-4.8-4z"/><path d="M16.8 14h2.2"/><path d="M18.4 12.4l1.7 1.6-1.7 1.6"/><path d="M7.2 14H5"/><path d="M5.6 12.4L3.9 14l1.7 1.6"/>',
    'eval': '<path d="M13.8 5.5c-2.6-.4-3.4 1.3-3.7 3.1L8 18.5"/><path d="M7.2 10.5h5.4"/><path d="M13.6 12.2l5.4 6.2"/><path d="M19 12.2l-5.4 6.2"/>',
    'geq': '<path d="M6 4.5v15"/><path d="M12 4.5v15"/><path d="M18 4.5v15"/><path d="M3.8 9.5h4.4"/><path d="M9.8 14.5h4.4"/><path d="M15.8 7.5h4.4"/>',
    'preamp': '<path d="M4.5 17.5L19.5 17.5L19.5 4.5Z" fill="currentColor" stroke-linejoin="round"/>',
    'device': '<rect x="7.5" y="3.5" width="9" height="17" rx="2"/><circle cx="12" cy="14.5" r="3"/><circle cx="12" cy="7.5" r="1.2"/>',
    'channel': '<path d="M4 6.5h9"/><path d="M4 12h7"/><path d="M4 17.5h9"/><path d="M14.5 15.5l2.8 2.8 4.2-6"/>',
    'stage': '<rect x="3" y="8.5" width="6" height="7" rx="1.5"/><rect x="15" y="8.5" width="6" height="7" rx="1.5"/><path d="M9.5 12h3.5"/><path d="M11.8 10.3L13.5 12l-1.7 1.7"/>',
    'delay': '<circle cx="12" cy="12" r="8"/><path d="M12 7.5V12l3 2"/>',
    'folder': '<path d="M4 8.5V6.5A1.5 1.5 0 0 1 5.5 5H9l2 2h5.5A1.5 1.5 0 0 1 18 8.5v1.5"/><path d="M3.2 10h16.4a1 1 0 0 1 .96 1.27l-1.6 5.7A1.6 1.6 0 0 1 17.4 18.2H5.1a1.6 1.6 0 0 1-1.54-1.18l-1.3-5.7A1 1 0 0 1 3.2 10z"/>',
    'pencil': '<path d="M14.2 5.3l4.5 4.5"/><path d="M4 20l1.1-4.2L16 4.9a1.4 1.4 0 0 1 2 0l1.1 1.1a1.4 1.4 0 0 1 0 2L8.2 18.9 4 20z"/>',
    'power': '<path d="M12 4v7"/><path d="M7.2 7.2a7 7 0 1 0 9.6 0"/>',
    'code': '<path d="M9 7l-5 5 5 5"/><path d="M15 7l5 5-5 5"/>',
    'save': '<path d="M5 4.5h11l3.5 3.5V19a1.5 1.5 0 0 1-1.5 1.5H6A1.5 1.5 0 0 1 4.5 19V6A1.5 1.5 0 0 1 5 4.5z"/><path d="M8 4.5V9h7V4.5"/><path d="M7.5 20.5v-6A1 1 0 0 1 8.5 13.5h7a1 1 0 0 1 1 1v6"/>',
    'import': '<path d="M12 3.5v9.5"/><path d="M8 9.5l4 4 4-4"/><path d="M4.5 15.5v2.5A1.6 1.6 0 0 0 6.1 19.6h11.8a1.6 1.6 0 0 0 1.6-1.6v-2.5"/>',
}
# the catalogue's type colours (tokens) and the pastel shelf values soft derives from them
TYPE = {
    'biquad': ('var(--type-biquad)', 'hsl(142 50% 62%)'), 'include': ('var(--type-include)', 'hsl(215 16% 62%)'),
    'vst': ('var(--type-vst)', 'hsl(271 50% 62%)'), 'copy': ('var(--type-copy)', 'hsl(189 50% 62%)'),
    'comment': ('var(--type-comment)', 'hsl(215 20% 62%)'), 'if': ('var(--type-if)', 'hsl(350 50% 62%)'),
    'eval': ('var(--type-eval)', 'hsl(199 50% 62%)'), 'geq': ('var(--type-graphiceq)', 'hsl(258 50% 62%)'),
    'preamp': ('var(--type-preamp)', 'hsl(38 50% 62%)'), 'device': ('var(--type-include)', 'hsl(215 16% 62%)'),
    'channel': ('var(--type-channel)', 'hsl(217 50% 62%)'), 'stage': ('var(--type-stage)', 'hsl(25 50% 62%)'),
    'delay': ('var(--type-delay)', 'hsl(174 50% 62%)'), 'text': ('var(--type-comment)', 'hsl(215 20% 62%)'),
}
# the minimal skin's row-head glyphs and the matrix bus letters
GLYPH = {'biquad': '~', 'include': '>>', 'vst': '[]', 'copy': '->', 'comment': '#', 'text': '·', 'if': '·', 'eval': '·',
         'geq': '·', 'preamp': '·', 'device': '·', 'channel': '·', 'stage': '·', 'delay': '·'}
BUS = {'biquad': 'B', 'include': 'I', 'vst': 'V', 'copy': 'C', 'comment': '#', 'text': 'R', 'if': 'F', 'eval': 'E', 'geq': 'G',
       'preamp': 'P', 'device': 'D', 'channel': 'C', 'stage': 'S', 'delay': 'D'}
EAR = {'biquad': 'FILTER', 'include': 'PATCH', 'vst': 'VST', 'copy': 'ROUTE', 'comment': 'NOTE', 'text': 'AUX', 'if': 'IF', 'eval': 'EVAL',
       'geq': 'GRAPHIC', 'preamp': 'PREAMP', 'device': 'DEVICE', 'channel': 'CHANNEL', 'stage': 'STAGE', 'delay': 'DELAY'}
CHANNEL = {'L': 'var(--channel-l)', 'R': 'var(--channel-r)', 'C': 'var(--channel-c)', 'LFE': 'var(--channel-lfe)', 'SL': 'var(--channel-sl)',
           'SR': 'var(--channel-sr)', 'RL': 'var(--channel-rl)', 'RR': 'var(--channel-rr)', 'SBL': 'var(--channel-sbl)', 'SBR': 'var(--channel-sbr)'}
MINIMAL_INK = {'L': 'var(--minimal-ch-l)', 'R': 'var(--minimal-ch-r)', 'C': 'var(--minimal-ch-c)', 'LFE': 'var(--minimal-ch-lfe)', 'SL': 'var(--minimal-ch-sl)',
               'SR': 'var(--minimal-ch-sr)', 'RL': 'var(--minimal-ch-rl)', 'RR': 'var(--minimal-ch-rr)', 'SBL': 'var(--minimal-ch-sbl)', 'SBR': 'var(--minimal-ch-sbr)'}

def svg(pict, size=None, extra=''):
    s = f' width="{size}" height="{size}"' if size else ''
    return f'<svg viewBox="0 0 24 24"{s} fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"{extra}>{PICT[pict]}</svg>'

def chip_ink(skin, ch):
    # One identity palette (ChannelIdentity.h): the header badges of studio, soft, rack and matrix wear the
    # routing colours as they are; minimal prints its console inks; ALL and unknown channels are channel-neutral.
    if skin == 'minimal': return MINIMAL_INK.get(ch, 'var(--minimal-ch-neutral)')
    return CHANNEL.get(ch, 'var(--channel-neutral)')

def channel_badges(skin, chans, virtual=()):
    """virtual: the channels the device does not have (ChannelIdentity::isVirtual); the previews assume a
    7.1 device (L R C LFE RL RR SL SR), so VC or SBL is virtual and L is not. Virtual badges are dashed."""
    out = []
    for c in chans:
        style = f'--c:{chip_ink(skin, c)}'
        cls = 'ch virt' if c in virtual else 'ch'
        out.append(f'<span class="{cls}" style="{style}">{c}</span>')
    return f'<span class="chs">{"".join(out)}</span>' if out else ''

def header(skin, kind, num, title, summary='', chans=(), collapsed=False, readout='', pict=None, cursor=False, enabled=True, extra='', power=True):
    """The shared header: [expand][number][power][+][-][code][badge][title][summary] ... [channels]."""
    t, p = TYPE.get(kind, TYPE['comment'])
    if skin == 'studio' and kind == 'biquad': t = 'var(--accent)'  # studio's BiQuad chip wears the row's band light
    pict = pict or kind
    badge_inner = svg(pict) if pict in PICT else f'<span class="mono3">{pict}</span>'
    numcell = {'matrix': f'{BUS.get(kind, "R")}{num}', 'minimal': f'{num}'}.get(skin, f'{num}')
    parts = [f'<span class="hb expand">{"&gt;" if collapsed else "v"}</span>',
             f'<span class="num{" cursor" if cursor and skin == "minimal" else ""}">{numcell}</span>',
             f'<span class="hb power">{svg("power")}</span>' if power else '',
             '<span class="hb">+</span><span class="hb">-</span>', f'<span class="hb code">{svg("code")}</span>',
             f'<span class="tbadge" style="--t:{t};--p:{p}">{badge_inner}</span>',
             f'<span class="title">{title}</span>']
    if summary: parts.append(f'<span class="summary">{summary}</span>')
    # the badge strip follows the title and summary, and the stretch after it owns the rest (FilterCardRow)
    parts.append(channel_badges(skin, chans))
    parts.append(extra)
    if readout: parts.append(f'<span class="readout">{readout}</span>')
    else: parts.append('<span class="spacer"></span>')
    return f'<div class="hdr">{"".join(parts)}</div>'

def row(skin, kind, num, title, body_html, summary='', chans=(), collapsed=False, readout='', pict=None, cursor=False, cls='', extra='', power=True):
    """One command row for one skin, with the skin's chrome furniture (glyph, ear, screws, led, rail lamp)."""
    furniture = ''
    classes = ['crow', cls]
    if skin == 'studio' and kind in ('biquad', 'preamp', 'delay', 'geq', 'copy', 'vst'): classes.append('lamp')
    if skin == 'studio' and kind == 'include': classes.append('dotted')
    if skin == 'minimal': furniture += f'<span class="glyph">{GLYPH.get(kind, "·")}</span>'
    if skin == 'rack':
        furniture += '<span class="screw tl"></span><span class="screw tr"></span><span class="screw bl"></span><span class="screw br"></span>'
        # the ear stencil: printed up the left ear only when the whole word fits between the bottom screw and the
        # SELECT LED on a unit at least 96 px tall; the channel card alone prints CHANNEL small (ADR 0001)
        furniture += f'<span class="ear{" small" if kind == "channel" else ""}"><span>{EAR.get(kind, "UNIT")}</span></span>'
        if kind not in ('comment', 'text', 'if', 'eval'): furniture += '<span class="led"></span>'
        if kind == 'vst': furniture += '<span class="plate">VST</span>'
    if skin == 'matrix':
        if kind in ('comment', 'text'): classes.append('remark')
        else: furniture += '<span class="lampdot"></span>'
    body = f'<div class="body">{body_html}</div>' if body_html and not collapsed else ''
    return f'<div class="{" ".join(c for c in classes if c)}">{furniture}{header(skin, kind, num, title, summary, chans, collapsed, readout, pict, cursor, extra=extra, power=power)}{body}</div>'

def frame(skin, inner, caption=None, cls='skin-list'):
    return f'<div class="skin skin-{skin} {cls}"><span class="skin-caption">{caption or skin}</span>{inner}</div>'

def page(name, group, height, subtitle, css, frames, width=960):
    html = [f'<!-- @dsCard group="{group}" height={height} width={width} subtitle="{subtitle}" -->',
            '<!doctype html>', '<html lang="ko">', '<head>', '<meta charset="utf-8">', f'<title>{name} — preview</title>', '<style>',
            '  html, body { margin: 0; background: transparent; }', css, '</style>', '</head>', '<body>', '<div class="ds-stack">']
    html += frames
    html += ['</div>', '</body>', '</html>', '']
    out = os.path.join(ROOT, 'project', 'components', name, 'preview.html')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = '\n'.join(html)
    open(out, 'w', encoding='utf-8').write(text)
    print('wrote', out, len(text), 'bytes')
