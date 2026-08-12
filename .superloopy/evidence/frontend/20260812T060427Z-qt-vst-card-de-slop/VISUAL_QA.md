# Visual QA

## Reviewed surfaces

| Surface | Result | Review |
| --- | --- | --- |
| Studio dark/light | PASS | Clean header identity, no raw strip, path, or `ABS` badge. |
| Minimal light | PASS | Structured card stays compact; no raw source band. |
| Soft dark | PASS | Plug-in identity and bus controls retain skin hierarchy. |
| Rack dark | PASS | Module treatment remains intact without development-path metadata. |
| Matrix light/dark | PASS | `EXTERNAL DEVICE` structure remains; raw caption strip is absent. |
| VST3 rejected | PASS | Short red unavailable/pass-through status; no backend dump. |
| VST2 fixed bus | PASS | Short amber notice and clearly labeled repair action. |
| Korean Studio | PASS | New status and action strings render without clipping. |

## Mobile review artifacts

- `mobile-skin-gallery.png` — one accepted VST3 state across all five skins.
- `mobile-state-gallery.png` — accepted, rejected, and VST2 fixed-bus states.
- `mobile-ko-state-gallery.png` — the same state review in Korean.

## Explicit negative checks

The curated structured VST captures were reviewed for, and do not show:

- `Raw` / `원본`;
- a serialized `VSTPlugin:` or `Library` command;
- `C:\`, `R:\`, repository/build paths, or the `ABS` badge;
- the former `Accepted bus` / `Active bus` diagnostic repetition.

Actual unmodeled or dynamic fallback rows may still expose their source by
design. Native window chrome and assistive-technology output are not represented
by the offscreen client-pixel gallery.
