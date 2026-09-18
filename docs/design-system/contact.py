"""Stack the five skins' gallery shots of one scene into a contact sheet (dark and light).
  python contact.py <galleryDir> <scene> [<scene> ...]   -> contact/<scene>-dark.png, contact/<scene>-light.png"""
import os, sys
from PIL import Image, ImageDraw
if len(sys.argv) < 3:
    sys.exit('usage: contact.py <galleryDir> <scene> [<scene> ...]')
g = sys.argv[1]; out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'contact'); os.makedirs(out, exist_ok=True)
sys.argv = [sys.argv[0]] + sys.argv[2:]
SKINS = ('studio', 'minimal', 'soft', 'rack', 'matrix')
def sheet(scene, mode, maxh=1700):
    ims = []
    for s in SKINS:
        p = os.path.join(g, f'{s}_{mode}_{scene}.png')
        if os.path.exists(p): ims.append((s, Image.open(p).convert('RGB')))
    if not ims: return None
    gap, label = 8, 16
    W = max(im.width for _, im in ims) + 8
    H = sum(im.height + gap + label for _, im in ims)
    canvas = Image.new('RGB', (W, H), (128, 128, 128)); d = ImageDraw.Draw(canvas)
    y = 0
    for s, im in ims:
        d.text((4, y + 2), f'{s} {mode} {scene} {im.width}x{im.height}', fill=(255, 255, 0))
        y += label
        canvas.paste(im, (4, y)); y += im.height + gap
    if H > maxh:
        r = maxh / H; canvas = canvas.resize((int(W * r), maxh), Image.LANCZOS)
    path = os.path.join(out, f'{scene}-{mode}.png'); canvas.save(path); return path
for scene in sys.argv[1:]:
    for mode in ('dark', 'light'):
        p = sheet(scene, mode); print(p)
