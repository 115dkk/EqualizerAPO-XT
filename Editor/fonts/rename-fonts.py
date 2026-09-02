"""Give the bundled fonts private family names so a same-named system install
can never merge into the Editor's families (Qt keys its font database by
family name; the last registration wins, and a user's system Pretendard /
DM Sans / variable fonts then serve the Editor's text or break weight
matching). Only the name table (and the CFF FontName for OTF) changes; the
glyphs and metrics are untouched. Run from the repository root; see
README.md in this directory."""
import glob
import sys

from fontTools.ttLib import TTFont

RENAMES = [
    # (family as shipped, private family, PostScript family stem as shipped, private stem)
    ("DM Sans", "EAPO Sans", "DMSans", "EAPOSans"),
    ("DM Mono", "EAPO Mono", "DMMono", "EAPOMono"),
    ("Pretendard", "EAPO Sans KR", "Pretendard", "EAPOSansKR"),
    ("Sarasa Mono K", "EAPO Mono K", "SarasaMonoK", "EAPOMonoK"),
]

TEXT_IDS = (1, 3, 4, 16, 18, 21)   # family, unique id, full name, typographic family, mac full, WWS family
PS_IDS = (6, 20)                   # PostScript name, compatible full (Mac)


def rename(path):
    font = TTFont(path)
    name = font["name"]
    changed = 0
    for record in name.names:
        text = record.toUnicode()
        original = text
        if record.nameID not in TEXT_IDS + PS_IDS:
            continue
        for family, private, stem, private_stem in RENAMES:
            # PostScript-style records (no spaces) take the stem first, so a
            # family whose stem equals its name (Pretendard) stays spaceless.
            if record.nameID in PS_IDS or record.nameID == 3:
                text = text.replace(stem, private_stem)
            text = text.replace(family, private)
        # Sarasa's PostScript names are dashed (Sarasa-Mono-K-Bold).
        text = text.replace("Sarasa-Mono-K", "EAPO-Mono-K")
        if text != original:
            record.string = text
            changed += 1
    if "CFF " in font:
        cff = font["CFF "].cff
        for i, ps_name in enumerate(cff.fontNames):
            for family, private, stem, private_stem in RENAMES:
                ps_name = ps_name.replace(stem, private_stem)
            if ps_name != cff.fontNames[i]:
                cff.fontNames[i] = ps_name
                changed += 1
        top = cff.topDictIndex[0]
        for key in ("FamilyName", "FullName"):
            if hasattr(top, key):
                value = getattr(top, key)
                for family, private, stem, private_stem in RENAMES:
                    value = value.replace(family, private)
                if value != getattr(top, key):
                    setattr(top, key, value)
                    changed += 1
    font.save(path)
    return changed


if __name__ == "__main__":
    files = sorted(glob.glob("Editor/fonts/*.otf") + glob.glob("Editor/fonts/*.ttf"))
    if not files:
        sys.exit("no fonts found; run from the repository root")
    for path in files:
        print(path, "records changed:", rename(path))
