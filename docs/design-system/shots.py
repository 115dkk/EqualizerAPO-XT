"""Headless render check: screenshot every _check page in both themes."""
import os, re, glob, sys
from playwright.sync_api import sync_playwright
root = os.path.dirname(os.path.abspath(__file__))
outdir = os.path.join(root, '_check', 'shots'); os.makedirs(outdir, exist_ok=True)
names = sys.argv[1:] or sorted(os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(root, 'project', 'components', '*', 'preview.html')))
with sync_playwright() as p:
    b = p.chromium.launch()
    for n in names:
        first = open(os.path.join(root, 'project', 'components', n, 'preview.html'), encoding='utf-8').readline()
        h = re.search(r'height=(\d+)', first); h = int(h.group(1)) if h else 400
        w = re.search(r'width=(\d+)', first); w = int(w.group(1)) if w else 960
        pg = b.new_page(viewport={'width': w, 'height': h})
        for attempt in range(4):
            pg.goto('file:///' + os.path.join(root, '_check', n + '.html').replace(os.sep, '/'))
            pg.wait_for_timeout(400)
            ok = pg.evaluate('[...document.styleSheets].every(s => { try { return s.cssRules.length > 0 } catch (e) { return false } }) && document.styleSheets.length >= 3')
            if ok: break
            print(n, 'sheet load incomplete, retrying')
        pg.evaluate('document.fonts.ready')
        pg.wait_for_timeout(600)
        for theme in ('dark', 'light'):
            pg.evaluate(f'document.documentElement.setAttribute("data-theme","{theme}")')
            pg.wait_for_timeout(150)
            pg.screenshot(path=os.path.join(outdir, f'{n}-{theme}.png'), full_page=True)
        full = pg.evaluate('document.documentElement.scrollHeight')
        print(f'{n}: declared height {h}, content height {full}')
        pg.close()
    b.close()
