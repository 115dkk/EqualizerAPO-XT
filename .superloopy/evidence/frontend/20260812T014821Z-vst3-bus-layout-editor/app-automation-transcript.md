# Qt application interaction transcript

Candidate: `befe7cf1e47caca0e15c3770e5df931cd2718b14`

## Automated widget journey

Command: `Editor.exe --selftest-vst` with Qt offscreen QPA.

- PASS — Input and Output selectors are discoverable by stable object names.
- PASS — visible selections present `Stereo -> 7.1`.
- PASS — both selectors expose accessible names and label buddies.
- PASS — focus traversal orders Input before Output.
- PASS — a keyboard Down action changes Input and emits one paired
  Input/Output contract.
- PASS — the actual VST2 state disables both selectors and preserves an
  explanatory tooltip on the disabled region.
- PASS — the visible `Remove ignored layouts` action emits the repair signal,
  reaches the card slot, clears both serialized keys, and does not reintroduce
  `StereoInput`.
- PASS — ModernCards migrates `StereoInput 1` to `Input Stereo Output Auto`;
  LegacyRows retains its lossless round trip.
- PASS — ChunkData and parameter maps survive every legacy and modern store.

Result ledger: `TEST_RESULTS.md`.

## Real plug-in host journey

`HybridConvTests.exe` loads deterministic VST2/VST3 modules and passed 1,635
suite checks. Its `Vst3HostTests` block passed 101 checks, including accepted
`Stereo -> 7.1`, rejected layouts, inconsistent metadata rejection, semantic
4.1/5.0 distinction, and the newly exposed accepted arrangement diagnostics.

Result ledger: `TEST_RESULTS.md`.

## Model/catalog regression journey

`EditorLogicTests.exe` passed 2,967 checks, including paired contract state,
Auto/Auto intent, removal, legacy migration, explicit-contract precedence,
picker/catalog exposure, and adjacent Editor models.

Result ledger: `TEST_RESULTS.md`.
