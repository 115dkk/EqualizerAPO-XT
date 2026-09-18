"""Stages the design system's binary inputs from the Editor sources so the tree under project/ is complete:
the modern icon set, the DM Sans / DM Mono files, the KS X 1001 subsets of the Korean faces (the artifact caps a
font file at 1 MB) and the app icon as PNG. Run from anywhere; needs fontTools (with brotli) and Pillow.

  python docs/design-system/stage-assets.py
"""
import os, shutil, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..'))
project = os.path.join(here, 'project')
fonts_src = os.path.join(repo, 'Editor', 'fonts')
icons_src = os.path.join(repo, 'Editor', 'icons', 'modern')

icons_dst = os.path.join(project, 'assets', 'Icons'); os.makedirs(icons_dst, exist_ok=True)
for f in sorted(os.listdir(icons_src)):
    if f.endswith('.svg'):
        shutil.copyfile(os.path.join(icons_src, f), os.path.join(icons_dst, f))
print('icons:', len([f for f in os.listdir(icons_dst) if f.endswith('.svg')]))

fonts_dst = os.path.join(project, 'fonts'); os.makedirs(fonts_dst, exist_ok=True)
for f in ('DMSans-Regular.ttf', 'DMSans-Medium.ttf', 'DMSans-SemiBold.ttf', 'DMSans-Bold.ttf', 'DMMono-Regular.ttf', 'DMMono-Medium.ttf'):
    shutil.copyfile(os.path.join(fonts_src, f), os.path.join(fonts_dst, f))

# The Korean faces: keep ASCII, Latin-1, general punctuation, the won sign, Hangul compatibility jamo and every
# precomposed syllable; drop hinting; write WOFF2. Each lands around 0.5-0.7 MB, under the artifact's 1 MB cap.
UNICODES = 'U+0020-007E,U+00A0-00FF,U+2010-2027,U+2030-205E,U+20A9,U+3131-318E,U+AC00-D7A3'
def subset(src, dst):
    cmd = [sys.executable, '-m', 'fontTools.subset', src, f'--unicodes={UNICODES}', '--flavor=woff2', f'--output-file={dst}', '--layout-features=*', '--no-hinting']
    subprocess.run(cmd, check=True)
for w in ('Regular', 'Medium', 'SemiBold', 'Bold'):
    subset(os.path.join(fonts_src, f'Pretendard-{w}.otf'), os.path.join(fonts_dst, f'Pretendard-{w}-subset.woff2'))
for w in ('Regular', 'Bold'):
    subset(os.path.join(fonts_src, f'SarasaMonoK-{w}.ttf'), os.path.join(fonts_dst, f'SarasaMonoK-{w}-subset.woff2'))
for f in sorted(os.listdir(fonts_dst)):
    size = os.path.getsize(os.path.join(fonts_dst, f))
    print(f'font: {f} {size} bytes' + ('' if size <= 1_000_000 else '  OVER THE 1 MB CAP'))

from PIL import Image
im = Image.open(os.path.join(repo, 'Editor', 'icons', 'app-icon.ico'))
im.size = max(im.info.get('sizes', {im.size})); im.load()
logos_dst = os.path.join(project, 'assets', 'Logos'); os.makedirs(logos_dst, exist_ok=True)
im.convert('RGBA').save(os.path.join(logos_dst, 'app-icon.png'))
print('logo:', im.size)
