# Visual QA — VST3 bus-layout editor

Platform: Microsoft Windows 10 Pro 10.0.19045 x64

Qt version: 6.9.3 locally; repository CI pins 6.10.1

Style: `CustomStyle` over Qt Fusion, with the existing studio, minimal, soft,
rack, and matrix QSS/renderer identities

DPR: logical 96 DPI contract (`AA_Use96Dpi`); client-pixel gallery capture

Locale: deterministic English matrix and a separate `ko_KR` studio matrix

Theme: all five skins in dark and light modes

Graphics backend: Qt offscreen raster

Capture method: the real candidate `Editor.exe --skin-gallery` builds real
`FilterCardRow`/`VSTCardEditor` widgets, loads deterministic VST2/VST3 binaries,
waits for layout/polish events, and grabs QWidget client pixels. The full
English run wrote and self-checked 1,370 PNGs; Korean studio wrote and
self-checked 274 PNGs. Curated originals and mobile contact sheets are retained
under this evidence root.

Window size: the gallery host is 960 x 720 logical pixels; individual card
captures use their live size hints.

Exercised states: baseline missing plug-in; VST3 accepted `Stereo -> 7.1`;
VST3 rejected with pass-through copy; VST2 disguised with a `.vst3` suffix,
selectors disabled, ignored-key warning, and removal action; normal, hover, and
disabled card states throughout the full gallery; English plus Korean long
status copy; all five skins in dark/light modes.

Findings/fixes:

- The old card had no visible bus controls. The new panel is directly below
  plug-in identity and remains inside every card's existing content region.
- Status cannot rely on color: `Accepted`, `Rejected`, and `VST2 ignores` are
  explicit text in addition to token-owned green/red/warning colors.
- Actual ABI was made visible in evidence by renaming the VST2 fixture to
  `LegacyDisguised.vst3`; the card still reports VST2 and disables selectors.
- Korean rejection and VST2 warning strings fit without clipping or horizontal
  overflow in the 960 px card fixture.
- No unexpected horizontal scrollbar, clipping, blank render, or shot-count
  mismatch occurred in the passing offscreen matrices.

Unverified surfaces:

- The evidence is QWidget client-pixel/offscreen proof, not OS-level native
  window chrome, native menus/dialogs, or compositor proof. A supplemental
  Windows-QPA run produced client images but returned the gallery's generic
  nonzero status without a diagnostic, so it is not promoted as passing proof.
- No accessibility-tree or screen-reader session was available. Accessible
  names, label buddies, keyboard traversal, disabled explanations, and
  text-equivalent status are behavior-checked; assistive-technology support is
  not claimed.
- High-DPI monitor transitions, RTL, touch/pen, and other OS versions are not
  claimed. The changed control is a Windows desktop mouse/keyboard Qt Widgets
  surface.

## Artifact map

- `mobile-skin-gallery.png` — accepted state across all five skin identities.
- `mobile-state-gallery.png` — accepted, rejected, and disguised-VST2 states.
- `mobile-ko-state-gallery.png` — the same state family rendered in Korean.
- `post/offscreen/*.png` — curated English dark/light source captures.
- `post/ko-KR/*.png` — curated Korean source captures.
- `baseline/*.png` — pre-change studio card without bus controls.
