"""Local render check: compile project/tokens.json into a tokens.css that mirrors the
page's compiler closely enough for previews, then write _check/<Name>.html copies of
every preview with tokens.css + bundle.css injected, plus an index page."""
import json, os, re, glob, io
root = os.path.dirname(os.path.abspath(__file__))
proj = os.path.join(root, 'project')
d = json.load(open(os.path.join(proj, 'tokens.json'), encoding='utf-8'))
themes = [t['id'] for t in d['color']['themes']]
first = themes[0]
def val(t, theme):
    v = t['value']
    if isinstance(v, str): return v if theme == first else None
    return v.get(theme)
def css(v):
    m = re.fullmatch(r'\{([A-Za-z0-9_.-]+)\}', v or '')
    return f'var(--{m.group(1)})' if m else v
out = io.StringIO()
out.write(f':root, [data-theme="{first}"] {{\n')
for t in d['color']['tokens']:
    out.write(f'  --{t["name"]}: {css(val(t, first))};\n')
out.write('}\n')
for th in themes[1:]:
    out.write(f'[data-theme="{th}"] {{\n')
    for t in d['color']['tokens']:
        v = val(t, th)
        if v is None: v = val(t, first)  # inherit the first theme
        out.write(f'  --{t["name"]}: {css(v)};\n')
    out.write('}\n')
out.write(':root {\n')
for fam in ('spacing', 'radius'):
    for t in d[fam]['tokens']:
        out.write(f'  --{t["name"]}: {t["value"]};\n')
for k, v in d['type']['families'].items():
    out.write(f'  --font-{k}: {v};\n')
out.write('}\n')
for f in d['type']['fonts']:
    out.write(f'@font-face {{ font-family: "{f["family"]}"; src: url("../project/{f["file"]}"); font-weight: {f["weight"]}; font-style: {f.get("style","normal")}; }}\n')
for g in d['type']['groups']:
    for s in g['styles']:
        fam = d['type']['families'][s.get('family', g['family'])]
        out.write(f'.{s["name"]} {{ font-family: {fam}; font-size: {s["fontSize"]}; font-weight: {s["fontWeight"]};')
        if 'letterSpacing' in s: out.write(f' letter-spacing: {s["letterSpacing"]};')
        if 'lineHeight' in s: out.write(f' line-height: {s["lineHeight"]};')
        out.write(' }\n')
chk = os.path.join(root, '_check'); os.makedirs(chk, exist_ok=True)
open(os.path.join(chk, 'tokens.css'), 'w', encoding='utf-8').write(out.getvalue())
names = []
for p in sorted(glob.glob(os.path.join(proj, 'components', '*', 'preview.html'))):
    name = os.path.basename(os.path.dirname(p))
    html = open(p, encoding='utf-8').read()
    first_line = html.split('\n', 1)[0]
    assert first_line.startswith('<!-- @dsCard'), (name, first_line)
    inject = f'<link rel="stylesheet" href="tokens.css"><link rel="stylesheet" href="../project/components/bundle.css">'
    html2 = re.sub(r'<head>', '<head>' + inject, html, count=1)
    html2 = re.sub(r'<html([^>]*)>', rf'<html\1 data-theme="{first}">', html2, count=1)
    open(os.path.join(chk, name + '.html'), 'w', encoding='utf-8').write(html2)
    names.append((name, first_line))
idx = ['<!doctype html><meta charset="utf-8"><title>check</title><body style="font-family:sans-serif;background:#888;margin:0">',
       '<div style="position:sticky;top:0;background:#222;color:#fff;padding:6px 10px;z-index:9">'
       '<button onclick="setT(\'dark\')">dark</button> <button onclick="setT(\'light\')">light</button> '
       '<script>function setT(t){document.querySelectorAll("iframe").forEach(f=>f.contentDocument.documentElement.setAttribute("data-theme",t))}</script></div>']
for n, fl in names:
    h = re.search(r'height=(\d+)', fl); h = int(h.group(1)) if h else 400
    idx.append(f'<h3 style="margin:10px;color:#fff">{n}</h3><iframe src="{n}.html" style="width:960px;height:{h+8}px;border:0;display:block;margin:0 10px 20px"></iframe>')
open(os.path.join(chk, 'index.html'), 'w', encoding='utf-8').write('\n'.join(idx))
print('tokens.css bytes', len(out.getvalue()), '| previews:', [n for n, _ in names])
