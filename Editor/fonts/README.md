# Bundled fonts

The Editor embeds these typefaces (see `Editor.qrc`) and registers them at
startup through `SkinThemeData::registerBundledFonts`.

| File | Upstream family | Family name inside the file | License |
|---|---|---|---|
| `DMSans-*.ttf` | DM Sans | `EAPO Sans` | `OFL-DMSans.txt` |
| `DMMono-*.ttf` | DM Mono | `EAPO Mono` | `OFL-DMMono.txt` |
| `Pretendard-*.otf` | Pretendard | `EAPO Sans KR` | `LICENSE-Pretendard.txt` |
| `SarasaMonoK-*.ttf` | Sarasa Mono K (Hangul + ASCII subset) | `EAPO Mono K` | `OFL-Sarasa.txt` |

## Why the family names differ from upstream

Qt keys its font database by family name and merges an application font
into a same-named family that is already installed on the system. When a
user has Pretendard (or a variable DM Sans) installed, the system faces
take over the Editor's family: weight matching then follows whatever the
system copy reports, and the Editor's Korean text can land on a Thin face
or on the system default (field report, 2026-09-02: menus and labels in the
studio skin rendered in Noto Sans KR Thin on a machine with Pretendard
installed per-user). Private family names make the Editor's typography
independent of what is installed.

Only the `name` table (family, full, PostScript and unique-ID records) and
the CFF font name were changed; glyphs, metrics and hinting are the
upstream files'. The renamed files are Modified Versions under the SIL Open
Font License and do not use the upstream Reserved Font Names.

To regenerate after updating an upstream file, put the upstream file in
place and run `python Editor/fonts/rename-fonts.py` from the repository root
(needs fontTools). The names it writes are the ones in the table above;
`SkinThemeData.cpp` and every skin sheet reference those.
