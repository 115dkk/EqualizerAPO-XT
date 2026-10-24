"""Generate the EAPO Mono SemiBold and Bold faces that DM Mono does not have.

DM Mono ships Light, Regular and Medium only. Its design source has two
masters, Light and Medium, and Regular is an instance between them, so no
heavier design exists upstream. This script carries every point further
along the designer's own Light -> Medium direction,

    point = Medium + t * (Medium - Light),

so stems, counters and overshoots keep moving the way the design already
moves them, and the 600-unit advance stays fixed. t was chosen so the ink
of a 13 px line, measured through Qt's FreeType engine, takes the steps DM
Sans takes from its own Medium: SemiBold +17 %, Bold +31 %. Rebuilding the
Medium master through this pipeline (t = 0) reproduces the bundled Medium
within 0.2 % ink.

The source is source/DMMono-MASTER.glyphs of github.com/googlefonts/dm-mono
at b4483136c21dcc718234e29c193d2604ae18e6b2 (2020-03-11). Run from the
repository root with glyphsLib, ufo2ft, skia-pathops and ttfautohint-py
installed:

    python Editor/fonts/make-mono-weights.py <path to DMMono-MASTER.glyphs>

It writes Editor/fonts/DMMono-SemiBold.ttf and DMMono-Bold.ttf, hinted with
ttfautohint like the upstream files and renamed by rename-fonts.py.
"""
import copy
import importlib.util
import os
import subprocess
import sys
import tempfile

import glyphsLib
import ufo2ft

HERE = os.path.dirname(os.path.abspath(__file__))
# The head table's timestamps follow the source commit (2020-03-11T09:17:42Z),
# so a rerun writes byte-identical files.
os.environ.setdefault("SOURCE_DATE_EPOCH", "1583918262")
WEIGHTS = [
    # (style, t, OS/2 weight class)
    ("SemiBold", 0.6, 600),
    ("Bold", 1.1, 700),
]


def load_rename():
    sys.dont_write_bytecode = True  # keep __pycache__ out of Editor/fonts
    spec = importlib.util.spec_from_file_location("rename_fonts", os.path.join(HERE, "rename-fonts.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.rename


def masters(source):
    ufos = {ufo.info.styleName: ufo for ufo in glyphsLib.to_ufos(glyphsLib.GSFont(source), propagate_anchors=True)}
    if set(ufos) != {"Light", "Medium"}:
        sys.exit(f"expected the Light and Medium masters, found {sorted(ufos)}")
    return ufos["Light"], ufos["Medium"]


def extrapolate(light, medium, t):
    def step(m, l):
        return round(m + t * (m - l))

    out = copy.deepcopy(medium)
    for glyph in out:
        base = light[glyph.name]
        if len(glyph) != len(base) or any(len(a) != len(b) for a, b in zip(glyph, base)) \
                or len(glyph.components) != len(base.components) or len(glyph.anchors) != len(base.anchors):
            sys.exit(f"glyph {glyph.name} is not compatible between the masters")
        for contour, base_contour in zip(glyph, base):
            for point, base_point in zip(contour, base_contour):
                point.x = step(point.x, base_point.x)
                point.y = step(point.y, base_point.y)
        for component, base_component in zip(glyph.components, base.components):
            m = component.transformation
            l = base_component.transformation
            component.transformation = tuple(m[:4]) + (step(m[4], l[4]), step(m[5], l[5]))
        for anchor, base_anchor in zip(glyph.anchors, base.anchors):
            anchor.x = step(anchor.x, base_anchor.x)
            anchor.y = step(anchor.y, base_anchor.y)
        glyph.width = step(glyph.width, base.width)
    return out


def name_style(ufo, style, weight_class, t):
    info = ufo.info
    info.styleName = style
    info.openTypeOS2WeightClass = weight_class
    info.postscriptFontName = f"DMMono-{style}"
    info.postscriptFullName = f"DM Mono {style}"
    info.openTypeNameVersion = f"Version 1.000; EAPO weight extrapolated from the DM Mono masters (t={t:.2f})"
    info.openTypeNameUniqueID = None
    if weight_class == 700:
        info.styleMapFamilyName = "DM Mono"
        info.styleMapStyleName = "bold"
        info.openTypeNamePreferredFamilyName = None
        info.openTypeNamePreferredSubfamilyName = None
    else:
        info.styleMapFamilyName = f"DM Mono {style}"
        info.styleMapStyleName = "regular"
        info.openTypeNamePreferredFamilyName = "DM Mono"
        info.openTypeNamePreferredSubfamilyName = style


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rename = load_rename()
    light, medium = masters(sys.argv[1])
    with tempfile.TemporaryDirectory() as tmp:
        for style, t, weight_class in WEIGHTS:
            ufo = extrapolate(light, medium, t)
            name_style(ufo, style, weight_class, t)
            unhinted = os.path.join(tmp, f"DMMono-{style}.ttf")
            ufo2ft.compileTTF(ufo, removeOverlaps=True, overlapsBackend="pathops").save(unhinted)
            target = os.path.join(HERE, f"DMMono-{style}.ttf")
            subprocess.run([sys.executable, "-m", "ttfautohint", unhinted, target], check=True)
            print(target, "records renamed:", rename(target))


if __name__ == "__main__":
    main()
