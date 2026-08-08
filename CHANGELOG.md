# Changelog

**English** | [한국어](CHANGELOG.ko.md)

All notable changes to EqualizerAPO-XT since it was forked from TheFireKahuna's
equalizerAPO64 tree (last upstream commit `7156020`, 2025-12-16). Work on this
fork started on 2026-05-22.

Versions are bumped automatically by CI from Conventional Commits message
types, so some version numbers were skipped (1.7, 1.9, 1.12.1, 1.14, 1.16,
1.23, 1.25, 2.30.1, 2.31, and 2.32 were never released). Tags up to v1.10.1 carried a `-main.<run>` suffix; from v1.11.0 on,
tags are clean `vX.Y.Z` names. Installers for every version are on the
[Releases page](https://github.com/115dkk/EqualizerAPO-XT/releases).

## Unreleased

## v2.34.1 — 2026-08-08

- **The Hilbert and Velvet entries in the add-filter picker now explain
  themselves.** Both shipped without a description (the Soft skin's picker
  showed its generic "Choose the details after adding" caption); they now
  carry one like every other command, in English and Korean
  ([#254](https://github.com/115dkk/EqualizerAPO-XT/pull/254)).
- **The subwoofer template's picker entry is translated into Korean.** The
  "Subwoofer routing" template name and its "Speaker management" section
  were missing from the translation files. The rest of the subwoofer
  editor's strings are now registered for translation too, but still show
  English; translating them is scheduled follow-up work
  ([#254](https://github.com/115dkk/EqualizerAPO-XT/pull/254)).

## v2.34.0 — 2026-08-08

- **Channel pills in the Minimal skin's routing views are readable on every
  color.** Names drawn on the light pills (the slate used by non-channel
  path ids, LFE's amber, the cyan/green family) were printed in white and
  washed out - most visibly in the subwoofer dialog's output matrix; those
  pills now switch to dark ink while the darker pills keep white. The hues
  themselves are unchanged.
- **Subwoofer routing is now one configuration line instead of a page of Copy
  chains.** The new `SubwooferRouting:` command (issue
  [#246](https://github.com/115dkk/EqualizerAPO-XT/issues/246)) runs
  per-speaker-group crossovers, dedicated bass paths, preservation of the
  physical LFE input as its own source path, per-path gain/polarity/delay/EQ
  and an output summing matrix from a single JSON state, inline or from a
  `*.swxt.json` profile file. The built-in `Issue #246 - Front/Rear 4.1`
  preset reproduces the reporter's original low-level chain sample for sample
  (verified by an engine-level parity test), automatic headroom keeps the
  summed outputs below 0 dBFS, invalid states log an error instead of muting,
  and unreferenced channels pass through bit-exactly. The Editor shows a
  Subwoofer Routing card with its own badge and summary. The DSP lives in a new
  MIT-licensed SubwooferRoutingCore library shared with the plugin below.
- **A standalone Subwoofer Routing VST3 plugin ships with every release.** The
  MIT-licensed `EAPO XT Subwoofer Routing` plugin (in the `VST3\` folder of the
  install) runs the identical DSP core in any VST3 host, negotiates stereo
  through 7.1 layouts including 4.1, and exchanges the same JSON state as the
  native command, so presets move between Equalizer APO XT and a DAW
  unchanged. It exposes bypass, source-LFE gain/polarity/delay and headroom
  trim as host-automatable parameters; the full routing graph travels through
  the plugin state. It has no custom graphical editor yet - hosts show their
  generic parameter view.
- **VST3 hosting no longer mislabels 4.1 layouts as 5.0.** The host used to
  pick a speaker arrangement from the channel count alone, so five channels
  always negotiated as L/R/C/Ls/Rs and a 4.1 system's LFE was presented to
  plugins as a Center channel. Arrangements are now chosen from the actual
  channel names (`L R LFE RL RR` negotiates k41Music), the accepted
  arrangement is read back from the plugin instead of assumed, and an
  explicit per-arrangement channel mapping routes each engine channel to the
  right plugin bus slot.

## v2.33.1 — 2026-08-08

- **Moving a card up or down is instant again.** Dragging one card to a new
  position stalled for seconds on a loaded configuration (5-6 s reported on
  a fast desktop) because the move rebuilt every card in the list; the move
  now splices only the affected rows, and the moved card keeps its expanded
  state. Measured on a 174-row document: 0.8-0.9 s down to 24-82 ms per
  move, in every skin.
  ([#253](https://github.com/115dkk/EqualizerAPO-XT/pull/253))
- **Drives in the file dialog sidebar show the drive glyph again.** Since the
  sidebar gained the drive roots, every drive rendered with the folder
  pictogram in the skinned dialogs; drives now carry each skin's dedicated
  drive glyph. ([#253](https://github.com/115dkk/EqualizerAPO-XT/pull/253))

## v2.33.0 — 2026-08-07

- **Checked checkboxes show a real check mark in every skin.** They used to
  render as a plain filled accent square, so on/off read through color
  alone; the partially-checked state now shows an accent dash instead of
  looking unchecked, and the square menu indicators of the Minimal and
  Matrix skins carry the same glyph.
  ([#252](https://github.com/115dkk/EqualizerAPO-XT/pull/252))


- **File dialogs accept a pasted path and carry Explorer-style shortcuts.**
  The location dropdown is now editable: paste a path copied from Windows
  Explorer (quotes from "Copy as path" included) and press Enter to jump
  there; a full file path selects that file. The sidebar adds the upstream
  Equalizer APO config folder, the folders of recently opened
  configurations, and every drive root with its Windows display name.
  ([#251](https://github.com/115dkk/EqualizerAPO-XT/pull/251))
- **Settings > Open program folder** shows the install directory in
  Explorer. ([#251](https://github.com/115dkk/EqualizerAPO-XT/pull/251))
- **The pencil button on an Include card now opens the included file in the
  editor.** It used to turn the config line into a raw path text field, while
  opening the file was hidden behind clicking the name. Editing the path as
  text is no longer offered on Include cards; choosing a different file stays
  with Browse/Locate.
  ([#251](https://github.com/115dkk/EqualizerAPO-XT/pull/251))
- **Card rows can be dragged from anywhere on their header.** Reordering by
  drag and drop only worked from the narrow strip around the row number,
  because the summary text consumed the mouse press and the drag hit-test
  area was offset by the card margins.
  ([#251](https://github.com/115dkk/EqualizerAPO-XT/pull/251))
- **Embedded VST3 panels are stable and can be closed.** The host no longer
  force-repaints the plugin view on every idle (the VST 3 view paints
  itself), parameter edits no longer cycle the plugin's activation state on
  every knob tick (the editor holds the documented Processing state for the
  whole session, so set values and the saved state stay consistent), and the
  panel button stays visible as "Close panel" while embedded instead of
  leaving un-embedding hidden in the options menu.
  ([#251](https://github.com/115dkk/EqualizerAPO-XT/pull/251))

## v2.30.2 — 2026-07-30

- **Saving a configuration no longer intermittently fails with "Access is
  denied" while the audio engine reloads it.** The Editor writes through a
  temporary file and atomically replaces the old configuration. Creating that
  temporary file wakes the engine's directory watcher, which could open the old
  file without allowing its directory entry to be replaced; if the Editor
  committed during that short read, Windows rejected the replacement. The
  Editor now retries only that atomic commit for a bounded period, while config
  readers keep rejecting in-place writes and retain a stable view of the bytes
  they opened. A failed save also records its path, error and exact
  open/write/commit stage in
  `%LOCALAPPDATA%\EqualizerAPO\logs\Editor.log`.

## v2.30.0 — 2026-07-27

- **Hilbert phase shifting is now a built-in filter with a complete Editor.**
  `Hilbert: Shift=SL,SR Align=L,R Direction=-90` runs a normalized 1025-tap
  linear-phase FIR on the explicitly shifted channels and applies its
  512-sample latency, without the phase transform, to explicitly aligned
  channels. Its independent card edits both channel roles and ±90° direction,
  reports the fixed FIR/latency contract, and can switch the analysis graph
  directly to phase or group delay.
- **Dynamic velvet-noise decorrelation is now a built-in filter with a complete
  Editor.** Every channel receives an independently seeded, sparse unit-energy
  FIR. Dynamic mode renews preallocated kernel banks at the Evolution interval
  and crosses between them with equal-power weights; Static mode keeps one
  deterministic bank. The independent card exposes amount, time spread and
  evolution up front, a deterministic impulse preview and correlation readout,
  and density, transition, decay and variation in a real expanding Advanced
  section. Frequency-response analysis freezes a Dynamic filter to one
  deterministic bank and labels the graph as a frozen Velvet snapshot.
- Both commands have strict round-tripping grammars, allocation-free real-time
  processing after initialization, independent picker entries, five-skin
  gallery coverage including invalid, expanded and 520 px states, and
  unit/integration/audio-reference tests. A separately distributable
  MIT-licensed VST3 uses the same independently implemented portable Velvet DSP.
- **A failed install's report no longer claims it created a registry key it did
  not.** Device Selector's log lists what an install wrote, and it counted a key as
  created whenever the install touched one - including keys that were already
  there. Nothing behaved differently; the line was simply wrong in the one place
  someone reads to work out what happened ([#243](https://github.com/115dkk/EqualizerAPO-XT/pull/243)).

## v2.29.0 — 2026-07-26

- **A configuration line the engine cannot use now says which line and why.** A
  broken `Convolution:` or `Copy:` line used to produce a log entry that guessed -
  "recognized but produced no filter, likely due to malformed parameters" - and on
  screen it looked exactly like a note somebody had typed into the file. The
  command that owns the line now reports what it expected, so the log names the
  file, the line and the reason, and hovering the card in the Editor shows the same
  sentence. Nothing else changes about how a configuration loads: the lines below a
  broken one still run, because half a configuration is better than none. ([#239](https://github.com/115dkk/EqualizerAPO-XT/pull/239))

## v2.28.0 — 2026-07-26

- **When installing onto a device fails, you can now find out why.** Device
  Selector performs the install, and it wrote no log at all: the error appeared in
  a message box, and the log file people were asked for had nothing about the
  install in it, because the Editor was not the program that ran it. Install,
  uninstall and repair now record what they found on the device, what they wrote,
  and what failed with which Windows error, into
  `%LOCALAPPDATA%\EqualizerAPO\logs\DeviceSelector.log`. The error dialog says
  where that is, and says whether the device was left as it was - or, in the one
  case where undoing the change also failed, that it may be left partly changed
  and needs a reboot before another attempt. ([#238](https://github.com/115dkk/EqualizerAPO-XT/pull/238))
- **`Editor.exe --diagnose` writes an install report.** It covers the install
  path, the COM registration, whether the audio engine (`audiodg.exe`, running as
  LOCAL SERVICE) and ordinary users can read the install and config directories,
  and which audio endpoints currently have Equalizer APO in their chain. The
  report goes to the console if you run it from one, and to
  `%LOCALAPPDATA%\EqualizerAPO\logs\diagnose-<time>.txt` either way. It changes
  nothing and needs no administrator rights, which is the point: the same checks
  previously required downloading `tools/Diagnose-EqualizerAPO.ps1`. Device
  Selector accepts the same switch, but it always asks for elevation, so the
  Editor form is the one to use. ([#238](https://github.com/115dkk/EqualizerAPO-XT/pull/238))
- **A Windows version check was wrong for Windows 10 and 11.** It packed each
  decimal digit of the version into its own group of bits, so a major version of
  10 or above did not compare correctly. Nothing had asked it about a version that
  high, so nothing was misbehaving; the new diagnostics report was the first
  caller to try, which is how it surfaced. ([#238](https://github.com/115dkk/EqualizerAPO-XT/pull/238))

## v2.27.5 — 2026-07-26

- **An installation that fails partway no longer leaves a device half connected.**
  Installing onto an audio endpoint performs around forty registry changes, and
  they used to run in a straight line: if one of them failed - an ACL on a
  property the driver owns, another process holding the endpoint key - whatever
  had already been written stayed written. The device could end up with our
  pre-mix effect in place and the driver's post-mix effect still there, and
  Device Selector reported it as installed, because finding either one is what
  installed means. Repairing an installation had the sharper version of the same
  problem: it removed the installation first, and a failure in the step after
  that left the device with nothing. Install, uninstall and repair now either
  finish or put the endpoint back as they found it. Two things a rollback cannot
  take back are documented rather than hidden: permissions widened to create a
  key on a driver-locked endpoint stay widened, and the `.reg` backup of the
  driver's own effect chain stays on disk, because it is the copy you would need
  to restore it by hand ([#236](https://github.com/115dkk/EqualizerAPO-XT/pull/236)).

## v2.27.0 — 2026-07-25

- The all-pass filter is now something you can see and set. It was always in the
  configuration reference, but everything about the Editor treated it as a row in
  an import-compatibility table: it sat among the level filters, it was created
  at `Q 10`, and the analysis graph - which only ever drew magnitude - showed it
  as a straight line at 0 dB no matter what it was set to. That is the correct
  magnitude, and it is also the whole problem, because level is the one thing an
  all-pass does not change ([#228](https://github.com/115dkk/EqualizerAPO-XT/issues/228)).
  - **The analysis graph switches between magnitude, phase and group delay.** The
    switch is in the analysis control bar. Nothing is measured again when you use
    it: one analysis produces all three readings.
  - **Phase and group delay can include or exclude the configuration's bulk
    delay.** The analyzer removes it before measuring, which is what makes a
    filter's own phase readable; a checkbox puts it back. A configuration that is
    only `Delay: 10 ms` reads as flat with the box off and as exactly 10 ms with
    it on.
  - **An all-pass line written as `BW Oct 1` used to come back as `Q 1`.** The
    engine had always accepted the bandwidth; the Editor's width selector offered
    the all-pass no way to keep it, so the number was read as a Q and saved as
    one. That is a different filter - about a factor of √2 less group delay at the
    centre frequency. Both editors now keep the spelling the line was written in.
  - **All-pass filters get their own card**, with no gain control, a statement
    that the magnitude is fixed at 0 dB, and a switch that puts the analysis graph
    on a reading where the filter is visible.
  - **New: 1st-order all-pass sections**, written `Filter: ON AP Fc 100 Hz Order 1`.
    A 1st-order section turns 180° and passes 90° at Fc; a 2nd-order one turns a
    full circle at any Q, so this was previously impossible to write. `Order` is
    optional and means 2 when absent, so existing configurations are untouched.
  - **`Delay` and the all-pass now live together under "Phase & Time"** in the
    filter picker, away from the filters that change level.
  - New all-pass filters are created at `Q 0.707` instead of `Q 10`. Existing
    configurations are never migrated.
  - Two defects found on the way: the analyzer allocated twice the FFT buffer it
    needed and read the uninitialized half on every run, and an unanalyzed graph
    drew a flat 0 dB response - a measurement that had not been taken.

## v2.26.5 — 2026-07-24

- The audio processing thread no longer waits on a lock, opens a file, or
  allocates memory. Loudness correction used to hand its updated filter
  coefficients across a mutex that a background thread held while calling into
  Windows for the current volume, so the audio thread could be made to wait on a
  lower-priority thread inside the Windows audio engine - the classic recipe for
  a dropout. The handoff is now lock-free. Three filters also wrote a log line
  from inside the audio callback, which opens and closes a file in `%TEMP%`, and
  they only did so at the exact moments least able to afford it: a format change
  or a plugin crash. Those lines now come out after processing stops, at the cost
  of appearing later than they used to
  ([#226](https://github.com/115dkk/EqualizerAPO-XT/pull/226)).
  - Output is unchanged. The nine golden regression cases reproduce their stored
    baselines to the same last digit as before the change.

## v2.26.4 — 2026-07-24

- The Editor and the audio engine now agree on what counts as a command. The
  engine has always been case-sensitive, so `preamp: -6 dB` is inert text to it
  the same way `copy: remember to re-measure` has been inert in Equalizer APO
  since 1.4.2. The Editor used its own looser rule, which meant it drew a live
  card for lines the engine never ran, and touching that card could turn a note
  into a real command. Those lines now show as plain text, matching what
  actually happens to your audio. Three things follow from the same fix
  ([#224](https://github.com/115dkk/EqualizerAPO-XT/pull/224)):
  - Numbered filter lines such as `Filter 1: ON IIR Order 2 Coefficients ...`
    open the IIR card again. REW and Dirac write this form by default, and it
    was falling through to the plain-text editor.
  - A commented-out line is only offered an enable toggle when enabling it
    would actually do something.
  - Keys such as `Channel 2:` are no longer treated as commands. The engine
    never ran them, and the Editor used to rewrite them into ones it would.
- The Preamp card reads and writes gain the way the engine does. `Preamp:
  -6,5 dB` written with a decimal comma is -6.5 dB to both, where the card
  previously read -6.0 dB and silently rewrote the line on the first knob turn.
  Exponent forms are read correctly, precision is no longer truncated
  (`-6.25 dB` stays `-6.25 dB`), and a line the engine cannot parse no longer
  opens a card that would overwrite it.

## v2.26.3 — 2026-07-24

- The Editor now recognises `MultiConvolution:` lines the way the audio engine
  does. Two of the filter's source files were never listed in the Editor's own
  build, so the Editor's copy of the engine had no `MultiConvolution` factory:
  the analysis panel drew the frequency response as if those lines were not
  there, and a commented-out `# MultiConvolution:` line was treated as an
  ordinary note instead of a disabled command, so the enable toggle did not
  bring it back. Playback was never affected, because the audio processing
  object is built from a different project that always had both files. A CI
  lint now compares the two source lists and fails the build when they drift
  ([#223](https://github.com/115dkk/EqualizerAPO-XT/pull/223)).

## v2.26.1 — 2026-07-23

- The real culprit behind "the toolbar quietly empties after switching
  skins a few times" is fixed (the previous release's fullscreen-latch fix
  closed a second, unrelated way to lose the bar). Visiting the Signal
  Matrix skin creates two full-width chrome overlay layers on the toolbar,
  and every skin sheet opens with a universal `QWidget { background }`
  rule: Qt's stylesheet polish makes any widget matching such a rule paint
  a framework-level opaque fill, bypassing the overlays' own do-not-paint
  guard. From the next skin switch on, the top overlay covered every
  toolbar control while all logical state (visibility flags, geometry,
  child order) stayed healthy — which is why flag-based checks kept
  passing while the screen lost the bar. The overlays, rack's rail-ear
  reserves and the toolbar spacers now refuse the framework background
  outright. The CI switch gate additionally judges rendered pixels — a
  toolbar grab that comes back near-uniform fails the build, verified to
  catch exactly this bug when the fix is removed — and a real-window
  diagnostic (`--skin-switch-storm`) drives the actual menus with
  synthesized clicks, screenshots every step, and restores the user's
  preferences afterwards.
  ([#220](https://github.com/115dkk/EqualizerAPO-XT/pull/220))

## v2.26.0 — 2026-07-23

- The main toolbar no longer vanishes for good after a graph-fullscreen
  round trip. Entering fullscreen (Ctrl+Alt+F, right next to the skin
  shortcuts) hides the toolbar, which unchecked View > Toolbar through the
  visibility sync; leaving fullscreen then consulted that same checkbox, so
  the toolbar stayed hidden until the next restart — usually blamed on skin
  switching, since that is what those shortcuts are used for. Leaving
  fullscreen now restores the toolbar from its remembered pre-fullscreen
  state, the Fullscreen graph menu entry shows a check while the mode is on,
  and the toolbar is no longer draggable (an accidental handle drag could
  float it or squeeze it into the overflow popup). The skin-switch CI gate
  now also fails if any toolbar control goes missing across skin revisits.
  ([#219](https://github.com/115dkk/EqualizerAPO-XT/pull/219))
- The Settings menu gained an "APO settings" entry that launches the Device
  Selector, so installing or removing the APO on devices no longer requires
  hunting down the separate executable.
  ([#219](https://github.com/115dkk/EqualizerAPO-XT/pull/219))
- Disabled toolbar and menu actions now look disabled from the glyph itself:
  the shared tinted icons (and soft's colour tiles) bake a faded variant for
  the disabled state, and minimal's text-only commands grey out their type.
  Previously undo/redo on an empty history only dimmed the button chrome
  slightly, so the buttons still read as clickable.
  ([#219](https://github.com/115dkk/EqualizerAPO-XT/pull/219))

## v2.25.2 — 2026-07-23

- Windows builds now define `NOMINMAX` before any direct or transitive
  inclusion of `windows.h`, preventing the legacy `min` and `max` macros from
  breaking standard-library and Highway headers. The local `#undef`
  workarounds are no longer needed.

## v2.25.1 — 2026-07-20

- The stereo-input upmixer option added in v2.25.0 is now reachable from the
  Editor: both the legacy row and the modern card gained a checkable "Stereo
  input" entry in their Options menu. Loading a config restores the checked
  state from `StereoInput 1`, saving serializes it back, and unchecking
  removes the option. Previously the Editor did not know the flag and dropped
  it on save, so a hand-edited `StereoInput 1` disappeared the moment the row
  was re-saved - there was no practical way for ordinary users to enable the
  feature. Round-trip coverage was added to --selftest-vst and the
  VSTPluginCommand tests.
  ([#215](https://github.com/115dkk/EqualizerAPO-XT/pull/215))

## v2.25.0 — 2026-07-20

- Upmixer plugins that want a DAW-style bus layout (a stereo input bus
  feeding the full-width output bus) are now supported. Running the real
  OpenSpatial Upmixer binary on a CI runner (PR #213) showed it accepts a
  symmetric 7.1 layout but leaves its engine disengaged there, only playing
  the front pair; the engine fully engages with the asymmetric layout. Add
  `StereoInput 1` to the VSTPlugin config line to negotiate that layout;
  plugins that declare an Up-Downmix/Spatial/Surround VST3 subcategory get
  it automatically. The layout applies only when the plugin actually accepts
  it, and ordinary multichannel plugins keep symmetric buses so no device
  channels are lost.
  ([#214](https://github.com/115dkk/EqualizerAPO-XT/pull/214))

## v2.24.2 — 2026-07-20

- The VST3 host now negotiates plugin buses from the device's real channel
  count instead of forcing every plugin into stereo at load time. An upmixer
  that expects one 5.1/7.1 bus (stereo signal in L/R, the plugin fills the
  other speakers) used to be split into several stereo instances that each
  saw only two channels, so only the front speakers played. Such plugins now
  get a single full-width instance; setBusArrangements results are verified
  against the bus info the plugin reports, 7.1.2 (10ch) and 7.1.4 (12ch)
  layouts are newly supported, and plugins that reject every proposal fall
  back to their own preferred arrangement. A deterministic upmixer-contract
  test module guards this in CI.
  ([#212](https://github.com/115dkk/EqualizerAPO-XT/pull/212))

## v2.24.1 — 2026-07-17

- Opening a file dialog (Open, Save as, or any of the card file pickers) and
  then closing it hung the Editor whenever the skinned title bar was in use
  (the default). The custom-frame helper kept an application-wide native
  message filter installed while the dialog was being torn down, and its
  per-message window lookup recreated the dialog's native window mid-destroy,
  wedging the message loop. The filter is now removed the moment the dialog
  hides, before Qt destroys the native window. Sessions with the native title
  bar restored were never affected.
  ([#211](https://github.com/115dkk/EqualizerAPO-XT/pull/211))

## v2.24.0 — 2026-07-16

- VST3 hosting fills in the contract pieces many plug-ins depend on, adapted
  from the ripDZL fork's compatibility work
  ([ripDZL#1](https://github.com/ripDZL/EqualizerAPO-XT/pull/1)) and
  restated in this codebase's RAII idiom. GUI parameter edits now actually
  reach audio processing (they used to stop at the controller), a
  single-component plug-in is no longer initialized and terminated twice
  (a crash-grade lifecycle bug), the host manufactures the
  IMessage/IAttributeList objects split plug-ins use to talk between their
  component and controller, controller-private state (UI preferences) is
  saved and restored next to the component state, and the Windows module
  lifecycle (InitDll/ExitDll, factory host context) is honored. A saved
  VST3 chunk that carries controller-private state uses a new combined
  layout: older versions load their own chunks fine here, but such a new
  chunk does not load on older versions. A deterministic VST3 test module
  and 43 host-contract checks guard all of it in CI.
  ([#210](https://github.com/115dkk/EqualizerAPO-XT/pull/210))

## v2.23.0 — 2026-07-16

- The skinned file dialog got its second design round from user review.
  Each skin now answers the folder/file pictograms itself instead of
  borrowing the shell set: studio engraves thin receded strokes into the
  glass, minimal draws square hairline glyphs that feel like a terminal
  without being ASCII art, rack shelves skeuomorphic objects (a manila
  folder, paper sheets with a real turned corner, a metal drive slab), and
  matrix renders chamfered CRT glyphs over its faint board grid. Rack's
  navigation row also wears the same machined transport caps as its main
  toolbar. The dialog's native Windows caption is replaced by the same
  skinned title strip the main window wears — title text plus the
  conventional close X — with move, snap and edge-resize staying native
  (the registry escape hatch that restores the native caption applies to
  dialogs too).
  ([#208](https://github.com/115dkk/EqualizerAPO-XT/pull/208))
- The Editor finally sheds its 2005-era notepad-and-pencil application
  icon: a new response-curve badge (a peaking curve with a node handle,
  the card view's own identity) ships as a multi-resolution icon on the
  executable — window, taskbar, Explorer — and on the installer produced
  by Velopack.
  ([#208](https://github.com/115dkk/EqualizerAPO-XT/pull/208))

## v2.22.0 — 2026-07-16

- Every file dialog in the Editor — Open and Save as, plus the impulse
  response, Include, VST and frequency-response pickers on the cards — now
  follows the active skin instead of popping the stock Windows dialog.
  Under a skin the dialog switches to Qt's widget implementation, so it
  wears the skin's surfaces and typography, and each skin answers the
  navigation buttons in its own language (the shared stroke icons by
  default, soft's pastel tiles, studio's receded ink). The sidebar starts
  with the active configuration root pinned first, then Downloads,
  Documents, Desktop and Home; the Detail columns finally get readable
  widths; and the dialog speaks the app's language (Korean included)
  instead of the OS locale only. The legacy-rows heritage presentation
  keeps the untouched native dialog on purpose.
  ([#207](https://github.com/115dkk/EqualizerAPO-XT/pull/207))

## v2.21.0 — 2026-07-16

- The main toolbar's skin dressing no longer depends on a skin switch
  actually happening: sessions that started on their saved skin came up
  with the legacy Windows-era icons and without the skin's toolbar chrome
  (rack's rail, MASTER label, ear zones, and the instant-mode pilot lamp),
  seemingly at random — dressed only if a skin or dark toggle had run since
  launch. The dressing now also runs directly on every preferences pass,
  and rack re-shows its rail zones without relying on a style-change event.
  A saved window state carrying a hidden toolbar (e.g. the app was closed
  while the graph was fullscreen) also stopped eating the save/tools row:
  every session now starts with the toolbar visible.
  ([#205](https://github.com/115dkk/EqualizerAPO-XT/pull/205))
- Undo/redo for the filter list is finally visible: the main toolbar
  carries undo/redo buttons after New/Open/Save (nudging the instant-mode
  toggle right), and both the buttons and the Edit-menu entries now grey
  out when the active tab's history has nothing to step to — previously
  they only lived in the Edit menu and always rendered enabled, silently
  doing nothing on an empty history.
  ([#205](https://github.com/115dkk/EqualizerAPO-XT/pull/205))
- After a config migration, restored tabs, recent files, and their per-file
  view preferences kept pointing into the old legacy folder — the one the
  audio pipeline no longer reads, so editing a restored tab silently changed
  nothing. Saved paths under the migrated folder now remap to their copies
  in the new configuration root on startup, copying a file over on demand
  when the migration's referenced-set import did not carry it.
  ([#205](https://github.com/115dkk/EqualizerAPO-XT/pull/205))

## v2.20.0 — 2026-07-16

- EqualizerAPO-XT now has its own configuration root:
  `%LOCALAPPDATA%\EqualizerAPO-XT\config`. The fork used to keep reading the
  registry config path a legacy Equalizer APO install had claimed, so edits
  to the XT install's own `config\config.txt` did nothing while the old
  `Program Files` folder kept driving the audio — even after the legacy APO
  was uninstalled. On install and on every update, the hook now classifies
  the trusted path: a legacy Equalizer APO folder has its `config.txt`
  imported with everything it references (Include chains at any depth,
  Convolution and MultiConvolution impulse responses, VST references,
  folder structure preserved) and the trust is repointed, after which the
  old folder is no longer read; a config folder inside a Velopack
  `current\` dir is rescued wholesale, since updates recreate `current\`
  and would have deleted it; a folder the user chose on purpose is left
  alone. The Editor shows a one-time notice after a migration, and
  `Editor.exe --migration-dry-run` prints what would happen on a machine
  without changing anything. The import scanner also learned
  `MultiConvolution:` lines and quoted convolution paths.
  ([#204](https://github.com/115dkk/EqualizerAPO-XT/pull/204))
- Opening a large configuration in the card view took over ten seconds on
  older CPUs (reported on an i7-7700K). The Editor was resolving its skin
  stylesheet against every card widget several times over: cards were built
  parentless and re-styled when moved into the table, the finished table was
  re-styled again when inserted into the tab bar, each card re-dressed its
  frame after its ~40 child widgets already existed, and the startup path
  re-applied the identical skin over the fully built window. Cards are now
  built in place, dressed before their children exist, and a same-skin
  re-apply is skipped, cutting a 300-row load from about 4.5 s to 1.5 s and
  a full skin switch over 138 rows from 1.8 s to 0.7 s on the same machine
  (proportionally larger on older CPUs). The whole-table channel scan also
  no longer walks the layout quadratically, and the CI skin-switch
  regression gate tightened from 4 s to 2.5 s.
  ([#203](https://github.com/115dkk/EqualizerAPO-XT/pull/203))

## v2.19.0 — 2026-07-16

- The channel badges at the top right of a filter card sat on an opaque
  app-background rectangle (nearly black in dark skins) that cut into the
  card surface — an unnamed container picked up every skin's global widget
  background rule. The strip is now transparent in all skins. Rows inside a
  Channel: selection (other than ALL) also inherit the selection's badges,
  so which channels a group member affects is readable on the row itself;
  rows that name their own channels, like Copy, keep their own badges, and
  toggling a Channel:/If: line's power now refreshes the indent and badges
  of the rows below it immediately.
  ([#202](https://github.com/115dkk/EqualizerAPO-XT/pull/202))
- Device Selector launched right after changing the Editor skin came up in
  the previous skin, because the choice was only saved when the Editor
  closed. The skin and dark-mode choice is now persisted the moment it
  changes. Devices can also be selected by double-clicking anywhere on the
  row — previously only the painted toggle at the row's right edge reacted,
  and nothing told you that. ([#202](https://github.com/115dkk/EqualizerAPO-XT/pull/202))
- GraphicEQ now transforms its synthesized impulse response once instead of
  once per output channel. Convolution and MultiConvolution likewise build one
  immutable frequency-domain filter bank per distinct referenced IR channel,
  while channel histories, mix buffers, and FFTW execution plans remain
  independent. Shared ownership keeps the bank alive without changing the
  AVRT path, DSP order, or output arithmetic.
  ([#201](https://github.com/115dkk/EqualizerAPO-XT/pull/201))
- Convolution, GraphicEQ, MultiConvolution, and large multi-channel IR
  preparation now use bounded multicore workers while a configuration is
  loading. Modern Cards prepare each row's parsed descriptor/scope once and
  cache fixed regexes and tinted SVG badges, reducing repeated work during
  skin rebuilds. HConv ownership now supports out-of-order initialization and
  rollback; the AVRT audio callback, DSP order, and output arithmetic are
  unchanged. CI covers worker exception cleanup and enforces a 4-second
  ceiling for the 138-row offscreen skin-switch stress test.
  ([#200](https://github.com/115dkk/EqualizerAPO-XT/pull/200))

## v2.18.6 — 2026-07-16

- Configuration saving is now atomic and partial reads are rejected instead
  of being applied as valid audio graphs. Filter factories, HybridConv/FFTW
  plans, IR data, Copy/IIR state, VST instances, COM apartments, and VST3 DLL
  factories now have explicit RAII ownership and checked external sizes.
  Failed filter initialization rolls back the whole reload and keeps the
  active configuration, so an allocation failure or malformed plug-in cannot
  escape the loader and terminate the Windows audio service. The DSP hot path
  and its operation order are unchanged. ([#198](https://github.com/115dkk/EqualizerAPO-XT/pull/198))

## v2.18.5 — 2026-07-15

- The Soft skin's add-filter picker described each template with the raw
  config line it would insert (`Filter: ON PK Fc 100 Hz Gain 0 dB Q 10`
  under "Peaking filter", `Delay: 0 ms` under "Delay"), which is noise to
  anyone not editing the syntax by hand. Every template now carries a
  one-line, translated description of what it does ("Boosts or cuts a band
  around a center frequency"), keyed off the command token so it stays
  correct as the catalog grows; the raw line still lives in the row's
  tooltip. ([#197](https://github.com/115dkk/EqualizerAPO-XT/pull/197))

## v2.18.4 — 2026-07-13

- Six confirmed CodeQL findings were fixed: allocation sizes computed from
  external data are validated against overflow before memory is requested,
  and exceptions no longer cross COM boundaries.
  ([#195](https://github.com/115dkk/EqualizerAPO-XT/pull/195))

## v2.18.3 — 2026-07-12

- Backend hot-path optimizations; output is bit-identical to before (the
  audio regression references did not change). Stereo — and any channels
  left over from a SIMD group — now run through a dual-chain biquad kernel
  instead of one latency-bound channel at a time, which the AVX2/AVX-512
  builds had been leaving fully scalar (a stereo 20-filter chain drops from
  0.44% to 0.25% of one core at 48 kHz). Convolution partition spectra
  moved from hundreds of per-partition heap blocks into two contiguous
  64-byte-aligned slabs, pre-touched at load so the audio thread's first
  block takes no soft page faults. The 1-4 channel interleaved float/double
  conversions now use explicit Highway SIMD (stereo: read 195→64 ns, write
  195→122 ns per 480-frame block on AVX2). Each fix is pinned by a new
  regression test, and the benchmark scenarios plus the measurement record
  are committed under `Benchmark/scenarios/` and `docs/perf/`.
  ([#192](https://github.com/115dkk/EqualizerAPO-XT/pull/192))
- The ARM64 build's interleaved write conversions had been running at scalar
  speed. The NEON kernel now writes full-width vectors, matching the read
  direction. ([#193](https://github.com/115dkk/EqualizerAPO-XT/pull/193))

## v2.18.1 — 2026-07-11

- Uninstalling on Windows 11 24H2/25H2 (and Server 2025, OS build 26100+)
  no longer fails on endpoints whose `FxProperties` key this installation
  created. The OS now puts its own subkeys below `FxProperties`, so the old
  whole-key delete threw a registry error: `DeviceSelector /u` blocked on a
  modal error dialog (forever when run unattended) and the app uninstall
  silently left the removed EQ APO CLSIDs dangling in the device's
  `FxProperties`. The uninstall now removes only the values it wrote and
  deletes the key only when nothing else lives in it, and `/u` reports
  registry errors through stderr and the exit code instead of a dialog.
  Reproduced and guarded by the live CI harness
  (`audio-live-repro.yml` with `runner=windows-2025`, issue
  [#189](https://github.com/115dkk/EqualizerAPO-XT/issues/189)).
  ([#191](https://github.com/115dkk/EqualizerAPO-XT/pull/191))

## v2.18.0 — 2026-07-11

- `MultiConvolution` mappings now take a per-file-channel factor with
  `Copy:`'s grammar: `L=0.5*0+1` halves file channel 0's convolution
  result before the sum, `-1` inverts the phase, `-0.5` does both, and dB
  values (`-6dB*0`) work. The Editor card opens the routing views' factor
  editing for these mappings, and an equivalence battery in
  AudioRegressionTests proves the one-liner bit-identical (SHA-256) to
  the manual Copy → Channel → Convolution → Copy fan-out it compresses,
  factors and stacked lines included - verified against real Impulcifer
  hrir.wav captures via `--equiv-ir`.
  ([#187](https://github.com/115dkk/EqualizerAPO-XT/pull/187))

## v2.17.0 — 2026-07-10

- Lines whose parameters hold an inline `` `expression` `` keep their card
  now. The Preamp and Delay cards open in a dynamic mode - the knob powers
  down and the value position shows the expression as written, with the
  computed value appearing in the analysis readouts - and every other editor
  stands down to the raw body instead of misreading the text. This also
  fixes a hazard where such a Preamp line displayed 0.0 dB and a single
  knob turn silently erased the expression.
  ([#184](https://github.com/115dkk/EqualizerAPO-XT/pull/184))

## v2.16.0 — 2026-07-10

- The programmatic commands are insertable from the filter picker: Eval joins
  Control, the If family gets its own Branching section, and both close the
  catalog after the processing filters. If/Eval rows carry their own
  pictograms - a flowchart decision diamond and an fx formula mark - instead
  of letter badges. ([#183](https://github.com/115dkk/EqualizerAPO-XT/pull/183))
- Custom-coefficient IIR filter lines ("Filter: ON IIR Order N Coefficients
  ...") got their own card: the header reads the order and coefficient count,
  and the body edits the order and both coefficient vectors directly. Other
  Filter lines keep their familiar knobs. ([#183](https://github.com/115dkk/EqualizerAPO-XT/pull/183))
- Korean translations cover the whole campaign, and the friendly condition
  sentences in the Soft skin are translated as complete sentences per
  comparison so the wording stays natural.
  ([#183](https://github.com/115dkk/EqualizerAPO-XT/pull/183))

## v2.15.0 — 2026-07-10

- Each skin now presents `If` blocks and `Eval` lines with its own
  instrument, and the analysis run reports what the engine actually decided:
  which branch ran, what an `Eval` computed, and which lines a false branch
  skipped. Rack runs a relay-switched power bus down the block with pilot
  lamps, Studio a gate beam of light, Minimal prints a TRUE/FALSE readout
  column and sinks skipped rows one background step behind dashed indent
  guides, Soft reads simple conditions as friendly sentences held by a pastel
  bar, and Matrix posts gate lamps, a printed bracket and cancelled-departure
  styling on skipped rows.
  ([#182](https://github.com/115dkk/EqualizerAPO-XT/pull/182))
- The Delay card body was modernized to match the Preamp card: a knob with
  the classic logarithmic sweep, a Time/Samples selector standing as the
  caption, and an editable value that now shows milliseconds with two
  decimals (0.25 ms no longer displays as 0.3 ms). The old body repeated the
  word "Delay" next to the dial, flush against the card edge.
  ([#182](https://github.com/115dkk/EqualizerAPO-XT/pull/182))

## v2.14.0 — 2026-07-10

- The programmatic config commands (`If:`/`ElseIf:`/`Else:`/`EndIf:`/`Eval:`)
  are cards now instead of anonymous raw-text rows: each line carries a badge
  for its branch kind (IF/ELIF/ELSE/ENDIF/EVAL) with the condition or
  expression as the card summary, and the rows inside an `If` block are
  indented like a channel group, nesting included. The per-skin presentations
  decided in the concept round (gate beam, watch readout, bracket rule and
  friends) come next; until they land the line body keeps the familiar raw
  editor. ([#178](https://github.com/115dkk/EqualizerAPO-XT/pull/178))

## v2.13.0 — 2026-07-09

- Copy cards stopped growing with the device. Every routing view used to lay
  the whole channel layout out flat, so on a 7.1 endpoint two lines of actual
  routing sat on an 8-row grid of empty cells. The views now fold: only the
  channels the command involves are shown (an empty Copy shows L/R as
  representatives), and the rest of the layout waits behind a reveal control
  in each skin's own grammar — a `+N CH` caption cell on the Matrix board, an
  expansion latch on the Rack faceplate, a pager fold line in the Minimal
  listing, a quiet "show more" pill in Soft, a ghost `+N` chip on the Studio
  glass. ([#175](https://github.com/115dkk/EqualizerAPO-XT/pull/175))
- Virtual channels can now be created and removed from every skin's Copy
  card, not just Studio's: each view gained an add-channel entry (Matrix
  `+BUS`, Rack `ADD`, Soft "Add channel", and Minimal's console prompt now
  actually takes input — click it and type the name), and hovering a virtual
  channel reveals a small remove target. Device channels never get one; they
  fold instead of leaving.
  ([#175](https://github.com/115dkk/EqualizerAPO-XT/pull/175))
- Fixed a card never growing when its body content grew: the editor's
  height-pinning wrapper updated itself but the row above it kept a stale
  minimum, which left the expanded routing view clipped.
  ([#175](https://github.com/115dkk/EqualizerAPO-XT/pull/175))

## v2.12.0 — 2026-07-09

- The GraphicEQ filter — the first thing a clean install shows — was rebuilt
  as a modern card. The response is a skin-painted instrument now, not a
  tinted stock plot: Studio draws a glowing glass analyzer, Minimal a 1px
  ink record on paper, Soft a rounded pastel well with boost/cut colours,
  Rack a genuine oscilloscope window (dark phosphor glass in both finishes),
  Matrix a cyan trace on the board's crisp grid. The full 20 Hz–20 kHz range
  always fits the card, the view frames itself around the response, and the
  old side table gave way to a selected-band readout strip (drag nodes on
  the plot, type exact values below). The Legacy rows mode keeps the
  original GUI untouched.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- The card header's `+` now inserts the new filter BELOW its card (it used to
  insert above, which read backwards), and inserting at the very top has its
  own entry: a slim insertion seam that appears when you hover just above the
  first card. The green legacy toolbar at the end of the list became a real
  skin-drawn "add card" row — a ghost glass slot, a terminal prompt line, a
  friendly pill slot, an empty rack bay or a vacant board cell depending on
  the skin. ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- Raw text lines (bare notes and programmatic commands like `If:`/`EndIf:`)
  stopped echoing a parameterless command twice, and each skin now presents
  them deliberately (Rack burns the line into a programming LCD, Matrix posts
  it as a board remark). A dedicated editor for the programmatic commands is
  planned separately.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- The automatic update finally announces itself: when a new release has been
  downloaded in the background, a small notice appears at the bottom of the
  editor saying it will be applied on exit.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- Device Selector now wears the Editor's skin (Studio by default) instead of
  the stock Windows dialog, and its troubleshooting options open with a
  chevron disclosure and a short slide instead of a checkbox.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- The Device Selector then went a step further: instead of a tinted stock
  list, every skin presents its own device-selection instrument, painted
  form-first with live hover on the rows, the dialog buttons and the
  disclosure. Hardware Rack is a literal patch bay (checking a device seats
  a plug in its 1/4" jack and patches a cable to the APO bus rail; hovering
  pre-heats the jack), Precision Minimal is a terminal device menu whose
  reverse-video cursor sweeps the hovered line, Signal Matrix is a
  target-acquisition board (corner brackets close in on the hovered node,
  checked ports energize their trace), Soft Lab is fear-free cards whose
  hover previews the exact outcome of a press, and Studio Glass is a glowing
  glass console. A `--skin-shots` harness renders all of it offscreen from
  canned devices for review and regression.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- Fixed skin switches getting slower and slower on configs with Copy lines:
  every rebuild leaked an invisible legacy Copy editor per Copy row, and
  every later switch restyled the growing pile. Thirty switches over a
  126-row config used to climb from 1.3 s to 18 s per switch; they now stay
  flat. CI gained a skin-switch stress gate that replays the live switch
  sequence and fails on a crash or a slow switch, so neither regression
  class can ship again.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- The analysis panel's response graph - the panel's last palette swap - became
  five instruments. Studio Glass monitors through a glowing glass pane whose
  over-0dB region warms with danger; Precision Minimal prints a plotter sheet
  (1px ink line, the overshoot marked as a terminal's reverse-video error
  block, crosshair annotation); Soft Lab shows a friendly pastel landscape
  (boost hills, cut valleys, and a plain-language warning that the sound may
  distort above 0 dB - no jargon); Hardware Rack seats a SPECTRUM MONITOR
  scope (green phosphor trace that burns danger-red above zero, a red PEAK
  lamp); Signal Matrix reads board telemetry (cyan trace, a hazard zone that
  densifies exactly where the response exceeds the bus, an OVER tag at the
  peak). The graph also gained a pointer readout: hover it to read the exact
  frequency and dB under the cursor, in each skin's own voice.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- Fixed the title bar's minimize/maximize/close buttons going near-invisible
  after switching between light and dark: the switch never re-tinted the
  caption glyphs (or the toolbar and Edit-menu icons), so a light-to-dark
  switch left dark glyphs on the dark strip. Every chrome icon now follows
  the switch, the application palette follows it too (menus and popups used
  to keep the startup palette), the analysis graph's response curve and the
  toolbar overflow arrow now follow the skin's dark flag instead of the OS
  theme, and the CI switch gate verifies the caption ink on every switch.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))
- Fixed the Legacy rows mode coming up dressed in the last skin's stylesheet
  on startup: the preference loader re-applied the saved skin over the
  freshly applied heritage presentation, which is exactly the
  modern-chrome-around-legacy-rows mixture v2.9.2 removed. A heritage
  session also no longer overwrites the saved skin choice.
  ([#172](https://github.com/115dkk/EqualizerAPO-XT/pull/172))

## v2.11.0 — 2026-07-05

- Reworked the Soft skin around its pastel grammar, following community
  feedback ("apart from the corner radius its identity is vague"): the accent
  and semantic colours are now pastel themselves, and every on/selected state
  is an opaque pastel fill carrying the deep warm ink instead of a translucent
  blue wash, in both dark and light modes.
  ([#170](https://github.com/115dkk/EqualizerAPO-XT/pull/170))
- Raised the dark-mode state contrast the same feedback called out: Studio's
  lit Device/Channel/Stage chips climb one brightness step, and Minimal's
  selection ground rises from #1f3554 to #2A4878. Rack and Matrix are
  unchanged. ([#170](https://github.com/115dkk/EqualizerAPO-XT/pull/170))
- Moved the analysis-panel controls from a full-width strip above the graph
  into a compact settings cell beside it, the original Equalizer APO panel's
  shape; a right-side dock stacks the cell above the graph instead.
  ([#170](https://github.com/115dkk/EqualizerAPO-XT/pull/170))
- The filter cards' type badges now carry the picker's pictograms (EQ shapes
  get their response-curve glyphs) instead of English letter chunks like PK
  or DEV, inked in each skin's own badge colour; the letters survive only as
  the fallback for unrecognised lines.
  ([#170](https://github.com/115dkk/EqualizerAPO-XT/pull/170))

## v2.10.1 — 2026-07-04

- Renamed the Korean Edit-menu labels for undo/redo from 실행 취소/다시 실행
  to 수정 취소/다시 수정: the stock Windows wording can read as if it were
  about launching a program, the new labels say what happens to the edits.
  ([#168](https://github.com/115dkk/EqualizerAPO-XT/pull/168))

## v2.10.0 — 2026-07-04

- The Editor's filter list supports undo and redo (Edit menu, Ctrl+Z /
  Ctrl+Y). Every edit counts: adding, deleting, pasting and dragging rows,
  knob drags, text edits and enable toggles, in both the Modern cards and
  Legacy rows modes. A knob drag or a typing run undoes as one step, and
  text fields keep their own in-field undo while focused.
  ([#166](https://github.com/115dkk/EqualizerAPO-XT/pull/166))

## v2.9.2 — 2026-07-04

- Restored the Legacy rows mode to the true heritage editor. It used to render
  the old rows inside the modern skinned chrome; it now starts with the native
  Windows style, standard title bar, system fonts, classic knobs and the
  original Copy node graph, with skins disabled while active. Switching between
  Modern cards and Legacy rows now restarts the editor to apply the whole
  presentation.
  ([#165](https://github.com/115dkk/EqualizerAPO-XT/pull/165))

## v2.9.1 — 2026-07-04

- Fixed a data race in the VST2 host: plugins running in different audio
  streams shared one global time-info structure, so a plugin asking for the
  current time could read a value another stream was writing. Each plugin
  instance now has its own.
  ([#163](https://github.com/115dkk/EqualizerAPO-XT/pull/163))

## v2.9.0 — 2026-07-04

- Device Selector and Update Checker now speak Korean (complete catalogs,
  including Qt's own dialogs) and follow the language you picked in the
  Editor instead of always using the Windows display language.
  ([#157](https://github.com/115dkk/EqualizerAPO-XT/pull/157))

## v2.8.1 — 2026-07-03

- Fixed a crash path inside the Windows audio engine: if EqualizerAPO could
  not load any configuration (for example an unreadable `ConfigPath` registry
  value), processing dereferenced a null configuration inside audiodg.exe and
  killed system audio. Audio now passes through unchanged instead.
  ([#150](https://github.com/115dkk/EqualizerAPO-XT/pull/150))
- `MultiConvolution` now shares the impulse-response cache with `Convolution`:
  a configuration reload reuses the decoded IR instead of re-reading every
  BRIR file from disk.
  ([#150](https://github.com/115dkk/EqualizerAPO-XT/pull/150))
- VST plugin setup survives out-of-memory conditions: a failed buffer
  allocation now degrades to passing audio through instead of crashing the
  audio service.
  ([#150](https://github.com/115dkk/EqualizerAPO-XT/pull/150))

## v2.8.0 — 2026-07-03

- Comment and Stage rows became real cards. A note line gets an in-place
  editor (and stays editable even though a comment row counts as disabled -
  a defect that had locked the body in every skin), and Stage rows pick
  their stages in two captioned pipeline lanes - Playback holds pre-mix →
  post-mix in signal order, Recording holds capture - writing the same
  bytes the old checkbox editor did; selectors the engine does not know
  survive as an inert chip instead of being dropped. Card bodies now hug
  their editor's height instead of padding compact cards with dead space.
  ([#141])
- The Device, Channel, Comment and Stage cards now wear each skin's own
  grammar instead of the neutral Phase 1 look: studio lights glass chips
  from within, minimal boxes mono tokens in hairline cells and inverts an
  engaged device seat like a console selection, soft reads on/off/sleeping
  as stadium pills, rack splits one machine into switch caps that press in
  and latch down, assign keys, insert-point jewels and a Dymo tape for
  notes, and matrix ranks the board's cells by material and type - its
  comment rows are remarks posted in sunken board cells, not amber
  "bypassed" flights. ([#141])
- The studio skin's Copy routing was redrawn as Light Trace: the 2015-era
  node graph (opaque candy pills, black wiring) gave way to lit glass chips
  joined by glowing curves of the skin's one accent light, with sunken
  factor readouts and a drawn ghost + for virtual outputs. Channels-to-
  channels lines stay; everything else is new. The MultiConvolution card
  inherits the look automatically. ([#141])
- The rack skin's Copy routing became a hardware routing-matrix button
  field: each crosspoint is a small square illuminated latching button in
  a recessed sub-panel - a routed point sits latched down, and only a
  non-unity gain is printed as the lit button legend (INV for a polarity
  flip, the bare coefficient otherwise, the danger lamp for negative
  gain); a unity routing lights a round LED lamp window with no legend,
  so nothing on the panel can be misread as a mute or a minus sign.
  Deliberately a different control from the filter cards' rotary dials.
  MultiConvolution's factor-less patch points use the same lamp window.
  ([#141])
- The minimal skin's Copy step list is staged as a console session
  instead of a bare table: zero-padded line numbers behind a gutter
  hairline, and a prompt with a steady block cursor closing the listing.
  ([#141])
- The skin gallery's judged set grew to 610 shots: Channel, comment, Stage,
  a populated Copy scene and two Device scenes over synthetic endpoints
  (a named selection and the all-devices master) now render for every skin
  in both modes. ([#141])

## v2.7.1 — 2026-07-03

- Fixed the Signal Matrix filter picker not highlighting the entry or
  category under the mouse. The hover pre-light was painted at roughly 3.5%
  brightness over the board - below what an eye can see - so only this
  skin's picker appeared to ignore the cursor. The pre-light now reads as
  an addressed cell at a glance while engagement (fill plus accent rule,
  band and patch trace) stays clearly above it. ([#142])

## v2.7.0 — 2026-07-03

- MultiConvolution no longer depends on the Channel command. The new mapping
  form `MultiConvolution: L=0+1 R=2+3 brir.wav` convolves each target
  channel's own signal with the listed 0-based channels of the impulse
  response file and sums back into that target, with several outputs and
  Copy-style virtual targets in one line; the one-token form
  (`MultiConvolution: L brir.wav`) stays valid and now means "every channel
  of the file", gated by the file itself. Behavior change from
  v2.5.0-v2.6.0, which read the channels selected by a preceding `Channel:`
  line - that pattern is still expressible with `Copy:` helpers, and the
  rewritten configuration reference shows the crossfeed recipe. ([#139])
- The MultiConvolution card edits the mapping in the same per-skin routing
  views Copy gets - studio's node graph, minimal's step list, soft's
  equation blocks, rack's patch-bay knobs, matrix's crosspoint grid - with
  the file's channels as fixed source ports and no gain factors (a patch
  point is either connected or not). Output ports are the channels in scope
  plus virtual channels added from the card; without a readable file the
  card explains itself instead of offering an edit. ([#139])

## v2.6.0 — 2026-07-02

- Reworked the file-reference rows (Include, Convolution, MultiConvolution,
  VST plugin) as per-skin reference cards. Every skin now presents the same
  facts - the target's name first, the location as a containing prefix
  (`Surround\`), a broken reference as a state transition with a Locate
  recovery entry, and an impulse-response readout - through its own
  construction: studio sets the identity over a sunken glass data window,
  minimal prints one terminal line, soft leads the row with a pictogram tile,
  rack builds a service-condition unit (status lamp, engraved captions, LCD
  readout window), and matrix runs a board feed line with an in-view port
  strip for VST. ([#137])
- Added a pictogram set (18 icons in the shared stroke grammar) covering the
  filter catalog: the eight biquad response curves, a distinct layered mark
  for MultiConvolution, and feature glyphs for Channel, Comment, Copy, Delay,
  Device, GraphicEQ, Loudness, Preamp and Stage. The Soft skin's filter
  picker and reference tiles now show these pictures instead of two-letter
  English monograms. ([#137])
- Fixed the rack skin painting an app-background rectangle under every custom
  faceplate widget (knobs, lamps, engraved labels, LCD wells), which made
  them look like stickers instead of parts of the brushed plate - worst
  around the Preamp knob. Also removed the value window that sat across the
  rack knob cap and cut the pointer line; the value already lives in the LED
  display beside the knob. The patch defect predates this rework and was
  visible in released rack builds. ([#137])
- The skin gallery's judged set grew to 460 shots: a Preamp row, nested and
  missing Include scenes and a resolvable Convolution scene now render for
  every skin in both modes. ([#137])

## v2.5.2 — 2026-07-01

- The MultiConvolution card now picks the output channel from a dropdown of the
  channels that exist at that point in the config, instead of a free-text box.
  The filter sums several inputs into one channel (one ear of a BRIR), so its
  output is almost always a channel that is already in play; the card presents
  those and still lets you type a custom or virtual channel name. The legacy row
  editor gets the same picker. ([#136])

## v2.5.1 — 2026-07-01

- Fixed the MultiConvolution filter's Editor UI, which shipped broken in 2.5.0.
  In the Insert menu it appeared under its own untranslated "Advanced filters"
  group instead of joining the existing one: the picker groups filters by their
  translated category name, and the new filter's category string had no
  translation, so it stayed in English while Convolution and Loudness correction
  showed the localized name. The German, French, Korean and Simplified Chinese
  catalogues now translate it, so the three share one Advanced filters group.
  Inserting the filter also produced a blank row, because the freshly dropped
  `MultiConvolution:` template has no channel or path yet: the Editor builds a
  filter card only once a legacy editor claims the line, and the strict parser
  rejected the empty line. The Editor now claims a `MultiConvolution` line by its
  keyword, the same way it already does for `Convolution`, and the card header
  shows a MultiConvolution badge instead of a generic text one. ([#132])

## v2.5.0 — 2026-07-01

- Added a MultiConvolution filter for BRIR (Binaural Room Impulse Response)
  playback. The existing Convolution filter only convolves each channel with
  its own in-place impulse response, so a stereo IR could not route one input
  into the other output channel; that collapses the crossfeed a BRIR needs
  (sound from one virtual speaker reaching the opposite ear). MultiConvolution
  convolves several selected input channels against the matching channels of
  one multichannel IR file and sums the results into a single output channel,
  so crossfeed survives. Config syntax is `MultiConvolution: <output channel>
  <multichannel IR path>`, where the first token names the output channel and
  the rest of the line is the IR path; a full binaural BRIR needs two of these
  filters plus a Copy to duplicate the input, one MultiConvolution per ear. The
  Editor gained both a modern card editor (output channel field, IR path field
  with a file picker, supported by all five skins) and a legacy row widget for
  this filter. ([#130])

## v2.4.2 — 2026-06-30

- The Editor now renders text with FreeType's grayscale antialiasing instead of
  Windows ClearType. The bundled Korean font (Pretendard) is a CFF/OpenType face,
  which ClearType draws with subpixel colour fringing that looks blurry on
  ordinary-density monitors; high-DPI panels (4K at 150%, say) packed enough
  pixels to hide it, so the blur only showed on lower-resolution screens.
  Switching the Editor to Qt's FreeType font engine removes the fringing and
  keeps text consistent from monitor to monitor. The device selector and update
  checker are unchanged: they use the system font, which ClearType renders
  cleanly. To go back to the old ClearType rendering, set the QT_QPA_PLATFORM
  environment variable to `windows`. ([#129])

## v2.4.1 — 2026-06-28

- Polished the Editor translations that shipped in 2.3.0. The channel
  configuration's "From device" option now reads as following the device's
  configuration in Korean, German and Chinese (it previously looked like a
  clipped fragment), a French string that had stayed in English ("VST plugin")
  is now translated, and several German/French/Chinese terms were made
  consistent (loudness vs volume, the Copy filter's assignment count, and the
  file-not-found wording). ([#128])

## v2.4.0 — 2026-06-27

- The Editor's translation catalogs now cover the whole interface in all four
  shipped languages (Korean, German, French, Simplified Chinese). Earlier builds
  translated only the menu bar and a handful of dialogs, so most of the modern
  card UI, the filter pickers, the per-filter editors (Channel, Copy, Device,
  Convolution, Include, VST, Graphic EQ, Loudness), the import dialog and the
  device/stream-format status messages stayed in English. Those strings are now
  filled in every catalog, and the filter-card titles and summaries (`Preamp`,
  `Copy`, `%1 bands`, ...) were made translatable so they localise too. Unit
  suffixes, numeric formats and the skin brand names are left in English on
  purpose. ([#126])

## v2.3.0 — 2026-06-26

- The Convolution filter row now has a modern card editor, matching the in-place
  style of the other filter cards instead of the old inline widget. It shows the
  impulse response's length and sample rate as soon as you pick a file and warns
  when that sample rate does not match the playback device. It also gains the
  same "import into the config directory" button the Include row already had:
  when the chosen impulse response sits outside the config folder - where the
  audio service has no read access - one click copies it in and repoints the path
  at the copy, so the convolution loads instead of silently failing on a file the
  service cannot read. ([#125])
- Fixed the dropdown and up/down arrows on combo boxes and spin boxes (the
  analysis bar's channel/position pickers and the resolution box, among others)
  rendering as a flat "-" instead of a triangle. The skins drew these arrows with
  a CSS-border triangle that collapses to a dash on Qt 6.10; they now use a
  chevron icon that renders reliably on every skin. ([#125])
- Korean text in the skins' monospace contexts now renders in a true fixed-width
  CJK face. The redesign's mono font (DM Mono) carries no Korean glyphs, so Korean
  used to fall back to the proportional Pretendard and broke the monospace grid.
  Sarasa Mono K (subset to Hangul + ASCII, OFL-1.1) is now bundled and sits ahead
  of Pretendard in the mono fallback chain, so monospace Korean stays on the grid.
  ([#125])

## v2.2.1 — 2026-06-21

- Fixed a memory-visibility data race on ARM64 where reloading the config during
  playback could hand the audio thread a partially-constructed filter
  configuration. The loader now publishes the new configuration to the real-time
  thread through a release/acquire flag, so the audio thread never reads it
  before its construction is visible. x86/x64 builds are unaffected (their memory
  model already ordered this). ([#124])

## v2.2.0 — 2026-06-21

- Hardened several security-audit findings on the Windows-facing surface. The
  Device Selector and Update Checker now load their Qt plugins from the
  executable's own folder instead of a path relative to the current working
  directory, closing a DLL search-order hijack that could run code with the
  elevated Device Selector's privileges. In the real-time audio engine a
  malformed config can no longer crash the audio service: an out-of-range
  `Delay:` value is clamped and its ring buffers are allocation-checked (audio
  passes through undelayed if allocation fails), and a `Convolution:` impulse
  response with no frames, no channels, or an implausible length is rejected
  before processing instead of dereferencing an empty buffer or spinning
  forever. ([#123])

## v2.1.0 — 2026-06-20

- Device filter rows now pick endpoints inline. Choosing which playback or
  capture devices a `Device:` line applies to no longer opens a separate
  dialog: the card body shows one checkable chip per endpoint plus an "All
  devices" chip, with devices that do not have the APO installed kept behind a
  "Show all" reveal. The chips are styled by each skin like the rest of the
  card instead of the native Windows device tree, and the written line stays
  byte-identical with what the old change-button dialog produced (a regression
  test in `EditorLogicTests` locks that serialization). ([#120])

## v2.0.1 — 2026-06-19

- Fixed a start-up crash (access violation) where the Editor failed to launch on
  some saved window layouts. `loadPreferences()` re-homed the analysis dock with
  `removeDockWidget()` + `addDockWidget()` both before and after
  `QMainWindow::restoreState()`; removing a dock that `restoreState()` had just
  laid out freed a layout item the dock area still referenced, and the first
  window show then dereferenced the dangling item (use-after-free). This was the
  #54/#75 start-up crash recurring on heavy or stale saved layouts. The analysis
  dock is now re-homed only when it is not already in the target area, removing
  the redundant churn without resetting anyone's saved layout. ([#118])

## v2.0.0 — 2026-06-18

- Fixed VST3 plugin editor windows being mis-sized on high-DPI (scaled)
  displays. The editor handed the plugin's physical-pixel size straight to a Qt
  widget that measures in logical (device-independent) pixels, so on a 150% or
  200% monitor the host frame came out too large and the plugin (for example
  FabFilter Pro-Q) rendered against a mismatched canvas, so a flat 0 dB EQ
  curve looked distorted and the panel had empty margins. The embedded panel,
  the "Open panel" dialog and the card embed now convert the plugin's physical
  size to logical pixels with the frame's device pixel ratio, keep the native
  host window in physical pixels, and tell DPI-aware plugins the host scale via
  `IPlugViewContentScaleSupport`. At 100% nothing changes. ([#108])
- Fixed a critical bug where uninstalling EqualizerAPO-XT could make all audio
  devices disappear from Windows until a reboot. The uninstaller restarted only
  the Windows Audio service, which left Windows Audio Endpoint Builder holding a
  stale endpoint graph that still referenced the just-removed APO, so an in-use
  endpoint became unusable until a reboot rebuilt the graph (the registry was
  already clean - the original driver effects were restored). The uninstaller
  now restarts Windows Audio Endpoint Builder to rebuild the live graph, so
  audio devices are no longer lost. A dispatch-only CI workflow reproduces this
  on a real virtual endpoint with audio in use and validates the fix. ([#105])
- Fixed the system effect never actually loading on current Windows, which
  left the EQ silently inactive and made the Device Selector install test fail
  on some endpoints with `Initialize failed for device "..." (the parameter is
  incorrect)`. Once the APO began exposing `IAudioSystemEffects2`, the audio
  engine started passing `Initialize` the larger `APOInitSystemEffects2`
  struct, but the APO still required the exact size of the base
  `APOInitSystemEffects` and rejected every init with `E_INVALIDARG`, so it
  bailed before loading. It now accepts the larger struct (all
  `APOInitSystemEffects` versions share the same leading fields), so the effect
  loads and processes audio again and the device test passes. ([#107])
- Fixed the editor crashing when switching to the minimal, soft, or rack skin,
  and intermittently on a plain restyle or dark-mode toggle. A skin switch
  tears the filter rows down before swapping the global stylesheet for speed,
  and the relayout the stylesheet triggers could re-enter the filter table's
  size-hint update while its grid layout was momentarily gone, dereferencing a
  null pointer. The update now does nothing while the rows are being
  rebuilt. ([#107])

## v1.27.1 — 2026-06-13

- Fixed a flaky start-up crash that could hit configs opened from an older
  version: the editor restored a saved window layout that no longer matched the
  current window structure (changed when the custom title bar moved the menu
  bar), and crashed during the first paint. The saved layout is now
  version-checked and ignored when it does not match, so the window simply opens
  with the default layout once. Also anchored the Qt plugin search to the
  executable's folder, so launching the editor from a working directory other
  than its install folder no longer fails with "no Qt platform plugin could be
  initialized". ([#98])

## v1.27.0 — 2026-06-13

- Adversarial design review round 1: the parameter area now belongs to each
  skin. Native spinbox arrows are gone from command rows (values drag-scrub),
  gain knobs read as bipolar with a 0 dB detent, and every skin reworked its
  rows and picker - studio band colors per filter type, minimal value-first
  hairline knobs with page-ordered picker numbering, soft warm-graphite dark
  identity, rack engraved captions with LCD value wells, matrix bus
  coordinates with a spec-echo caption strip. The offscreen gallery grew to
  250 captures and now fails the render on horizontal row overflow. ([#94])

## v1.26.0 — 2026-06-12

- The Editor draws its own window chrome: the native Windows caption is
  replaced by a skinnable title bar (dragging, snapping, edge resizing and
  double-click maximize stay native), and the menu bar plus every dropdown
  menu now follow each skin's design language — lit glass with luminous
  separators (Studio Glass), a terminal title line with icon-free mono menus
  (Precision Minimal), a calm rounded header and menu cards (Soft Lab), an
  engraved brushed panel with LED checks (Hardware Rack), and a grid masthead
  with cell menus (Signal Matrix). The Edit menu's remaining 2005-era icons
  were replaced with modern stroke icons. A "Native title bar" toggle in the
  Interface menu restores the stock caption after a restart. The offscreen
  gallery now captures title bar, menu bar and an open menu per skin with
  Korean sample text, guarding against the Hangul clipping reported from the
  field. ([#88])

## v1.24.0 — 2026-06-12

- The main toolbar lost its stock Windows look and 2005-era icons. A new
  `ISkin::styleMainToolbar` hook dresses it per skin: the top edge of the
  glass with light pooling under unboxed buttons (Studio Glass), a terminal
  command line with NEW/OPEN/SAVE as mono text commands (Precision Minimal),
  a calm header band with pastel tiles and a real stadium toggle (Soft Lab),
  a brushed master rail with transport buttons, screws and an LCD save-state
  well (Hardware Rack), and a board header of square function cells with a
  status lamp (Signal Matrix). The save-state badge is now styled by each
  skin (the old hardcoded pill is gone), and the offscreen gallery captures
  every toolbar. ([#85])

## v1.22.0 — 2026-06-12

- The "add filter" picker is no longer one flat list of every template: it is
  a compact dropdown anchored at the add button, and each skin presents the
  catalog in its own design language — a frosted-glass panel (Studio Glass),
  a numbered terminal index with digit-jump (Precision Minimal), a rounded
  settings menu (Soft Lab), a 1U module preset browser with an LCD search
  strip (Hardware Rack), and a two-axis crosspoint instrument (Signal
  Matrix). Skins contribute their picker through the new
  `ISkin::createFilterPicker` hook; the offscreen gallery captures every
  picker. ([#81])

## v1.21.0 — 2026-06-12

- Gain knobs (the Preamp card knob and the biquad gain dial) now turn across
  a configurable ±range, default ±20 dB, set via View > Interface > Knob gain
  range. Typed values still accept each command's full range and simply peg
  the knob; the previous fixed ±100 dB preamp span made small turns jump by
  tens of dB. ([#78])
- The analysis panel starts at a more modest height, docks at the bottom by
  default like the original Equalizer APO, and its position is picked from a
  Pos dropdown (Top / Bottom / Right) in the panel's control bar instead of
  the implicit Ctrl+Alt+G cycling. ([#78])
- A deliberately emptied Copy command can be refilled from the GUI in every
  skin. The crosspoint grid (Signal Matrix) and patch-bay (Hardware Rack)
  always offer the full device channel surface; the step list (Precision
  Minimal) and equation blocks (Soft Lab) seed a row per device channel and
  gained a per-row [+] menu for adding sources, and clearing a factor removes
  that source. Seeded empty rows write nothing to the config line. ([#78])

## v1.20.0 — 2026-06-12

- The Editor now writes a crash minidump and a small text report (version,
  exception address, the last skin switched to) to
  `%LOCALAPPDATA%\EqualizerAPO-XT\crashdumps` whenever it dies unexpectedly,
  instead of disappearing without a trace. This was added to hunt a
  machine-specific crash when selecting certain skins ([#75]); CI now also
  keeps debug symbols for every released binary so those dumps can be
  analyzed. ([#76])

## v1.19.0 — 2026-06-12

- The five Editor skins are now fully differentiated visual identities
  instead of palette variations. Each answers command-type marking, hover,
  disabled state, Include/VST presentation, corner language and hierarchy
  with its own shapes, textures and typography — glass cards with glowing
  arc knobs (studio), a zero-radius hairline terminal (minimal), a roomy
  rounded settings look (soft), skeuomorphic rack hardware with painted
  screws, nameplates and pointer knobs (rack), and a grid instrument panel
  with LED-ring encoders and crosspoint hover (matrix). Built by five
  isolated implementation agents and integrated after a differentiation
  review; the full record is in docs/skin-integration-report.md. ([#73])

## v1.18.0 — 2026-06-12

- Channel rows on the modern cards now edit their selection in place with
  checkable chips (device channels, ALL, custom/virtual names, plus a field
  to add new names) instead of requiring the raw-text editor or the legacy
  dialog flow. Equivalent selections serialize byte-identically to what the
  old dialog wrote. ([#70])
- Dropdowns no longer render undersized: all skins share a readable sizing
  floor, the toolbar dropdowns follow the system font size, and popup lists
  widen to their longest entry instead of eliding it. ([#70])
- Skin program Phase 0 plumbing landed for the five-skin overhaul
  (issues #66–#68): knob painting and command-row chrome are now delegated
  through `ISkin` hooks with appearance-preserving defaults (proven
  pixel-identical), and a headless screenshot gallery
  (`Editor --skin-gallery`) renders every skin's representative rows; CI
  uploads the images as the `skin-gallery` artifact. ([#70])

## v1.17.2 — 2026-06-12

- All accumulated cppcheck findings were triaged. Real defects fixed: a
  resource leak on the throwing paths of the file-access check used by the
  Include GUI, ARM64-native VST libraries being misreported as having the
  wrong architecture, and a `log10(0)` call in Benchmark for silent output.
  The rest of the tree received mechanical hardening (default member
  initializers, const-reference passing, explicit `wstring::npos` checks),
  and CI now runs a pinned cppcheck 2.21.0 as a blocking gate at a zero
  baseline. ([#64])
- The remaining filter config grammars (Stage, Include, Device,
  If/ElseIf/Else/EndIf, Eval and inline backtick expressions, IIR,
  LoudnessCorrection) moved into shared command codecs used by both the
  engine and the Editor, completing the migration started in #57. Each codec
  has round-trip tests, and the unused ParameterArchive helper was
  removed. ([#63])
- The `Channel:` and `Convolution:` config grammars moved into shared command
  codecs used by both the engine and the Editor, with round-trip tests. This
  fixed the Channel GUI ignoring comma-separated selectors. The
  `IFilterFactory` consumption contract and the intentional Editor/UpdateChecker
  update-path separation are now documented in the code. ([#57])
- The biweekly audit now runs on a Windows runner with Git Bash and a
  pre-provisioned buildable tree, so audits can compile and execute the test
  suites instead of reading the code blind. ([#58])
- `main` pushes that do not bump the version cannot produce a new release, so
  they now skip the build matrix entirely; a full-matrix build stays available
  via `workflow_dispatch`. ([#61])
- README refreshed to the current project state, this changelog added, and
  Korean translations of both provided. ([#60], [#62])

## v1.17.1 — 2026-06-11

First round of fixes from biweekly audit issue #53.

- The auto-detect installer now verifies downloaded setups against a
  `SHA256SUMS.txt` release asset before launching them; CI publishes the
  checksum file with every release. ([#56])
- libHybridConv buffer ownership moved from unguarded process-global maps into
  the convolution structs themselves, removing a latent data race between APO
  instances. Audio output is bit-identical. ([#56])
- New `EngineOrchestrationTests` suite covers channel-name routing, `Copy`
  semantics, and config-swap crossfading through the public engine API. ([#56])
- The CI build matrix is generated from the `.github/simd-variants.psd1`
  manifest, binary dependency downloads are tag-pinned and SHA-256-verified,
  and a lint step fails CI when installer channel names drift from the
  manifest. Pull requests now genuinely build only the primary avx2 variant
  (the old PR filter silently built all six). ([#55])
- The biweekly audit runs on Claude Fable 5. ([#54])

## v1.17.0 — 2026-06-10

- **Auto-detect installer**: a new front-door `EqualizerAPO-XT-Setup.exe`
  detects the CPU (architecture and AVX level) and downloads the matching
  build, so users no longer pick a SIMD variant by hand. The ARM64 update
  channel was aligned with the published `arm64-neon` name. ([#52])
- Second round of fixes from audit issue #48: a shared test harness with new
  regression/helper/parser coverage, a runtime VST2 host test built around a
  self-built test plug-in, the `VSTPluginInstance` god-file split into cohesive
  units, one owning parse routine for BiQuad config lines, a config-file codec
  extracted from MainWindow, shared parse/serialize codecs for the Preamp,
  Delay, GraphicEQ, Copy, and VSTPlugin GUIs, the SIMD variant set defined once
  in `simd-variants.psd1`, and qmake failing loudly when no SIMD flag is
  passed. ([#51])

## v1.15.3 — 2026-06-09

First round of fixes from audit issue #48. ([#50])

- The convolution IR cache is size-bounded and `ConvolutionFilter` resource
  handling was hardened.
- Filter factories register through a central registry, allocations go through
  a typed checked allocator, and the engine warns when a recognized command
  produces no filter. VST plug-in initialization failures are logged with
  reasons.
- The Editor's known-command list is derived from the factory registry instead
  of a hand-maintained copy, and the legacy filter-list UI path is frozen and
  documented. Release-notes SIMD channel tables are driven from one table.

## v1.15.2 — 2026-06-09

- Audit workflow actions updated for Node 24. ([#49])

## v1.15.1 — 2026-06-08

- Added a biweekly automated code-audit workflow that runs Claude against the
  codebase and files its findings as a GitHub issue. ([#46], [#47])
- The B-plan epic for runtime SIMD dispatch is recorded in
  `docs/RuntimeDispatchEpic.md`. ([#45])

## v1.15.0 — 2026-06-08

- **Portable SIMD with Google Highway**: the four hand-written SIMD sites
  (convolution kernels, BiQuadFilter, PreampFilter, float↔double conversion)
  were ported from per-ISA intrinsics to single Highway kernels compiled per
  variant. ARM64 moves from scalar fallbacks to real NEON. A new
  `convolution_short` regression case with a committed reference gates the
  port; output stays within the regression tolerance on all variants.
  ([#43], [#44])
- CI fails the build when a Qt application executable is missing instead of
  packaging an incomplete release. ([#44])

## v1.13.1 — 2026-06-08

- VST plug-in library load/unload is thread-safe. ([#42])
- User documentation was rewritten in English and Korean and is published to
  the GitHub Wiki by CI. ([#36], [#37], [#38], [#39])
- Audio regression reference data is committed, and cross-variant comparison
  fails on missing outputs instead of passing silently. ([#40], [#41])

## v1.13.0 — 2026-06-07

- **Native VST3 hosting** through the Steinberg VST3 SDK pluginterfaces, with
  double-precision processing where the plug-in supports it. ([#35])

## v1.12.x — 2026-06-03 ~ 2026-06-06

- Skin and theme switching no longer re-polishes the whole widget tree, making
  it near-instant. (v1.12.5, [#34])
- Modern filter card icons render correctly and Copy gain labels no longer
  overlap. (v1.12.4, [#33])
- The filter knob is a true rotary control that tracks the cursor, and release
  pipelines are serialized with a CI concurrency group. (v1.12.3, [#32])
- CI moved to the `windows-2025-vs2026` runner image with per-runner platform
  toolsets, GitHub Actions moved off deprecated Node.js 20, and
  `velopack_libc.dll` ships under the name the app actually loads — fixing
  startup of packaged builds. (v1.12.2, [#29], [#30], [#31])
- Legacy filter cards use the modern AudioKnob, channel selection badges are
  colour-coded, and Qt high-DPI scaling is enabled so the UI is not tiny on 4K
  displays. (v1.12.0, [#28])

## v1.11.0 — 2026-06-03

- **Automatic updates**: the Editor downloads new releases in the background
  through the Velopack SDK and applies them when the Editor exits. ([#27])
- Font weights render correctly and the Copy routing renderer swaps when the
  skin changes. ([#27])

## v1.10.x — 2026-06-02

- The APO reports its effect through `IAudioSystemEffects2::GetEffectsList`,
  so Windows can show what processing is active. (v1.10.0, [#25])
- Copy routing editors are seeded with the device channel list instead of an
  empty canvas. (v1.10.1, [#26])

## v1.8.0 — 2026-05-31 ~ 2026-06-02

- **Per-skin Copy routing renderers**: each of the five Editor skins renders
  channel routing with its own visual language, driven by a new `ISkin`
  delegation engine. DM Sans, DM Mono, and Pretendard fonts are embedded and
  QSS fonts are tokenized. ([#22])
- A flaky Editor startup crash was fixed by serializing FFTW planner access.
  ([#22])
- Non-realtime COM/Win32 resources are wrapped in RAII, and the APO registers
  itself in-process instead of shelling out to regsvr32. ([#23])

## v1.6.0 — 2026-05-26 ~ 2026-05-27

- The APO passes audio through unchanged when the sample format is not
  IEEE_FLOAT 32/64 instead of corrupting it, and the Editor surfaces the
  passthrough status so users can see when EQ is inactive. ([#21])
- Three Modern Card rendering bugs found by variant diagnostics were fixed,
  and the Modern Card right header toolbar is visible again. ([#19], [#20])

## v1.5.x — 2026-05-24 ~ 2026-05-25

- **Five distinct Editor skins** (studio, minimal, soft, rack, matrix) with
  per-skin token QSS, plus automated version bumping from Conventional
  Commits. (v1.5.0, [#11], [#12])
- All filter factories link from Common.lib (some filters silently did
  nothing before) and FFTW wisdom is cached. (v1.5.1, [#13])
- Performance passes over the DSP hot path, Editor analysis panel, and
  convolution: decoded impulse responses are cached per filter type, only the
  toggled row refreshes on enable/disable, and the Velopack update check is
  deferred 60 s from startup. (v1.5.1, [#14], [#15], [#16], [#17])
- Commented-out rows no longer grey the whole card and BiQuad cards show a
  richer summary. (v1.5.2, [#18])

## v1.4.3 — 2026-05-22 ~ 2026-05-24

- **Velopack migration (phases 1–5)**: headless APO registration, Velopack
  install/update/uninstall hooks in the Editor, raw binaries packed directly
  into Velopack, a runtime helper that triggers background updates, and the
  NSIS installer with its scheduled-task update path removed. ([#4])
- **AudioRegressionTests**: a regression suite that renders DSP scenarios and
  compares output against committed references, run in CI with cross-variant
  comparison and cppcheck; PR and push builds were split. ([#6])
- SSE2 and AVX release channels were added (threaded FFTW in lower SIMD
  builds), Velopack feed assets for AVX are recognized, legacy card editors
  were replaced, and default Qt styling was swept out of the Editor UI
  ([#3]).
- A stage-level profiler measures the audio pipeline in Benchmark. ([#5])
- An import-to-config flow scans a referenced config's dependencies and copies
  them into the config directory from the Include card. ([#7])
- The install directory grants LOCAL SERVICE access so audiodg can load the
  APO, with diagnostics and recovery scripts under `tools/`. ([#9], [#10])
- `setup-build.ps1` provisions a local build environment. ([#8])

## v1.4.2 — 2026-05-22

Fork bootstrap on top of TheFireKahuna's tree.

- **Convolution tail fix**: reverb tails no longer cut out around the 1000 ms
  mark after frame size changes, guarded by new hybrid-convolution regression
  tests. ([#2])
- Broad modernization refactor: RAII for filter configuration storage and COM
  objects, `nullptr`/typed casts/typed buffer copies, large implementation
  files split by responsibility, filter factories registered outside
  FilterEngine, and FilterEngine synchronization modernized. ([#1], [#2])
- Convolution file path handling was tightened and path parsing expanded.
  ([#2])
- A Velopack release workflow and update feed integration were added ([#1],
  [#2]). GitHub Actions builds were stabilized: dependencies download from
  release assets, Qt installs directly in CI, actions run on Node 24, and
  ARM64 builds use a native MSVC environment.
- First version of the modern card-based Editor UI.

[#1]: https://github.com/115dkk/EqualizerAPO-XT/pull/1
[#2]: https://github.com/115dkk/EqualizerAPO-XT/pull/2
[#3]: https://github.com/115dkk/EqualizerAPO-XT/pull/3
[#4]: https://github.com/115dkk/EqualizerAPO-XT/pull/4
[#5]: https://github.com/115dkk/EqualizerAPO-XT/pull/5
[#6]: https://github.com/115dkk/EqualizerAPO-XT/pull/6
[#7]: https://github.com/115dkk/EqualizerAPO-XT/pull/7
[#8]: https://github.com/115dkk/EqualizerAPO-XT/pull/8
[#9]: https://github.com/115dkk/EqualizerAPO-XT/pull/9
[#10]: https://github.com/115dkk/EqualizerAPO-XT/pull/10
[#11]: https://github.com/115dkk/EqualizerAPO-XT/pull/11
[#12]: https://github.com/115dkk/EqualizerAPO-XT/pull/12
[#13]: https://github.com/115dkk/EqualizerAPO-XT/pull/13
[#14]: https://github.com/115dkk/EqualizerAPO-XT/pull/14
[#15]: https://github.com/115dkk/EqualizerAPO-XT/pull/15
[#16]: https://github.com/115dkk/EqualizerAPO-XT/pull/16
[#17]: https://github.com/115dkk/EqualizerAPO-XT/pull/17
[#18]: https://github.com/115dkk/EqualizerAPO-XT/pull/18
[#19]: https://github.com/115dkk/EqualizerAPO-XT/pull/19
[#20]: https://github.com/115dkk/EqualizerAPO-XT/pull/20
[#21]: https://github.com/115dkk/EqualizerAPO-XT/pull/21
[#22]: https://github.com/115dkk/EqualizerAPO-XT/pull/22
[#23]: https://github.com/115dkk/EqualizerAPO-XT/pull/23
[#25]: https://github.com/115dkk/EqualizerAPO-XT/pull/25
[#26]: https://github.com/115dkk/EqualizerAPO-XT/pull/26
[#27]: https://github.com/115dkk/EqualizerAPO-XT/pull/27
[#28]: https://github.com/115dkk/EqualizerAPO-XT/pull/28
[#29]: https://github.com/115dkk/EqualizerAPO-XT/pull/29
[#30]: https://github.com/115dkk/EqualizerAPO-XT/pull/30
[#31]: https://github.com/115dkk/EqualizerAPO-XT/pull/31
[#32]: https://github.com/115dkk/EqualizerAPO-XT/pull/32
[#33]: https://github.com/115dkk/EqualizerAPO-XT/pull/33
[#34]: https://github.com/115dkk/EqualizerAPO-XT/pull/34
[#35]: https://github.com/115dkk/EqualizerAPO-XT/pull/35
[#36]: https://github.com/115dkk/EqualizerAPO-XT/pull/36
[#37]: https://github.com/115dkk/EqualizerAPO-XT/pull/37
[#38]: https://github.com/115dkk/EqualizerAPO-XT/pull/38
[#39]: https://github.com/115dkk/EqualizerAPO-XT/pull/39
[#40]: https://github.com/115dkk/EqualizerAPO-XT/pull/40
[#41]: https://github.com/115dkk/EqualizerAPO-XT/pull/41
[#42]: https://github.com/115dkk/EqualizerAPO-XT/pull/42
[#43]: https://github.com/115dkk/EqualizerAPO-XT/pull/43
[#44]: https://github.com/115dkk/EqualizerAPO-XT/pull/44
[#45]: https://github.com/115dkk/EqualizerAPO-XT/pull/45
[#46]: https://github.com/115dkk/EqualizerAPO-XT/pull/46
[#47]: https://github.com/115dkk/EqualizerAPO-XT/pull/47
[#49]: https://github.com/115dkk/EqualizerAPO-XT/pull/49
[#50]: https://github.com/115dkk/EqualizerAPO-XT/pull/50
[#51]: https://github.com/115dkk/EqualizerAPO-XT/pull/51
[#52]: https://github.com/115dkk/EqualizerAPO-XT/pull/52
[#54]: https://github.com/115dkk/EqualizerAPO-XT/pull/54
[#55]: https://github.com/115dkk/EqualizerAPO-XT/pull/55
[#56]: https://github.com/115dkk/EqualizerAPO-XT/pull/56
[#57]: https://github.com/115dkk/EqualizerAPO-XT/pull/57
[#58]: https://github.com/115dkk/EqualizerAPO-XT/pull/58
[#60]: https://github.com/115dkk/EqualizerAPO-XT/pull/60
[#61]: https://github.com/115dkk/EqualizerAPO-XT/pull/61
[#62]: https://github.com/115dkk/EqualizerAPO-XT/pull/62
[#63]: https://github.com/115dkk/EqualizerAPO-XT/pull/63
[#64]: https://github.com/115dkk/EqualizerAPO-XT/pull/64
[#70]: https://github.com/115dkk/EqualizerAPO-XT/pull/70
[#73]: https://github.com/115dkk/EqualizerAPO-XT/pull/73
[#75]: https://github.com/115dkk/EqualizerAPO-XT/issues/75
[#76]: https://github.com/115dkk/EqualizerAPO-XT/pull/76
[#78]: https://github.com/115dkk/EqualizerAPO-XT/pull/78
[#81]: https://github.com/115dkk/EqualizerAPO-XT/pull/81
[#85]: https://github.com/115dkk/EqualizerAPO-XT/pull/85
[#88]: https://github.com/115dkk/EqualizerAPO-XT/pull/88
[#94]: https://github.com/115dkk/EqualizerAPO-XT/pull/94
[#98]: https://github.com/115dkk/EqualizerAPO-XT/pull/98
[#105]: https://github.com/115dkk/EqualizerAPO-XT/pull/105
[#107]: https://github.com/115dkk/EqualizerAPO-XT/pull/107
[#108]: https://github.com/115dkk/EqualizerAPO-XT/pull/108
[#118]: https://github.com/115dkk/EqualizerAPO-XT/pull/118
[#124]: https://github.com/115dkk/EqualizerAPO-XT/pull/124
[#123]: https://github.com/115dkk/EqualizerAPO-XT/pull/123
[#120]: https://github.com/115dkk/EqualizerAPO-XT/pull/120
[#125]: https://github.com/115dkk/EqualizerAPO-XT/pull/125
[#126]: https://github.com/115dkk/EqualizerAPO-XT/pull/126
[#128]: https://github.com/115dkk/EqualizerAPO-XT/pull/128
[#129]: https://github.com/115dkk/EqualizerAPO-XT/pull/129
[#130]: https://github.com/115dkk/EqualizerAPO-XT/pull/130
[#132]: https://github.com/115dkk/EqualizerAPO-XT/pull/132
[#136]: https://github.com/115dkk/EqualizerAPO-XT/pull/136
[#137]: https://github.com/115dkk/EqualizerAPO-XT/pull/137
[#139]: https://github.com/115dkk/EqualizerAPO-XT/pull/139
[#141]: https://github.com/115dkk/EqualizerAPO-XT/pull/141

[#142]: https://github.com/115dkk/EqualizerAPO-XT/pull/142
