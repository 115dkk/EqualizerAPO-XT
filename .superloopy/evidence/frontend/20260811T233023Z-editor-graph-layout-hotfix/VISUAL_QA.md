# Visual QA

Platform: Microsoft Windows 11 Education x64, build 22621

Qt version: 6.10.1, Qt Widgets

Style: EqualizerAPO-XT `CustomStyle` wrapping Qt Fusion

DPR: 2.00 on the native target; 1.00 in the additional offscreen regression run

Locale: `ko_KR`

Theme: Rack dark for the full Editor analysis-dock capture; Studio, Minimal, Soft, Rack, and Matrix in both dark and light modes for card-selection captures

Graphics backend: Qt Widgets raster via the Windows QPA plugin. The comparison matrix additionally used Qt's offscreen QPA plugin.

Capture method: Win32 `PrintWindow` with Per-Monitor-V2 DPI awareness for the complete Editor window; native-QPA `QWidget::grab()` for the focused card scenes; offscreen `QWidget::grab()` only for deterministic before/after and CI regression evidence.

Window size: Analysis journey 1024x768 logical / 2048x1536 captured pixels. Card scenes 960x720 logical / 1920x1440 captured pixels.

Exercised states:

- Analysis dock at Right, moved to Bottom through the real position slot, then restored to Right.
- Right-dock controls at their full graph width and bottom-dock controls at their compact cap.
- Plain header click, Up-arrow navigation, an intentional three-card selection, and a plain click inside the third card's raw `QLineEdit`.
- Five skins in dark and light modes for the card journey.

Findings/fixes:

- Before: at 1024px wide the right dock took 862px, leaving 161px for filter cards, while the controls stopped at 250px inside the oversized dock. After: the native target leaves 629px for cards and gives a 394px dock to the graph; controls and graph are both 374px wide. The header, form controls, plot, and card workspace are all complete and unclipped in `after-right-native.png`.
- A 90-degree graph rotation was considered but rejected: the bounded 374px plot preserves the conventional frequency axis and is readable without that orientation cost.
- Before: a header click changed the model but left the original card chrome focused (`model=1 visual=0`), while a click in `QLineEdit` changed neither model nor chrome (`model=0 visual=0`). The before capture visibly shows the old first-card ring together with the third field focus.
- After: each of 10 skin/theme scenes reports one selected and one focused card on the clicked third row. Native captures show no stale ring on the first or second card.
- Human inspection found no clipped graph header, stranded left-aligned control strip, truncated card actions, unexpected horizontal scrollbar, palette regression, or duplicate card selection chrome in the exercised states.

Unverified surfaces:

- OS-level physical pointer injection was unavailable; native-process QMouseEvent automation exercised the actual widgets and selection code instead.
- 100%, 125%, 150%, and 250% scaling, Windows high-contrast mode, other locales, and other operating systems were not exercised. The changed target is the Windows Qt Editor, and the CI offscreen run supplies a second DPR/layout check only.
- No accessibility-tree claim was made: semantic roles, labels, tab order, and keyboard selection behavior were not changed. Up-arrow navigation was nevertheless covered as the closest interaction regression path.
- Packaging, install/update lifecycle, native system menus, and custom-window move/snap controls were outside this layout-and-selection patch.
