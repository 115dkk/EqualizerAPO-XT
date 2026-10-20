# Legacy expression separators

## Reproduction and decision

SECS exports channel mappings as `Eval: inL = 1; inR = 2`. At XT commit
`3a6a213c`, the real FilterEngine rejects the semicolon, then the conditions
using these variables fail. Stereo FIR and gain-only configurations leave the
input unchanged when their global preamp is 0 dB. A mono configuration works.
Splitting assignments into separate Eval lines restores the expected output
without changing the FIR files or filter body.

The official Equalizer APO 1.4.2 source archive refers to muparserx 3.0.1.
That parser accepts both semicolons and newlines as expression separators.
The muparserx commit `68c12e06f16aed9568392e3d4c02f8089bd29b7b`
removed semicolons on 2014-05-30, after the 3.0.1 release. XT uses 4.0.12.
Separate compiled probes of 3.0.1 and 4.0.12 reproduce this difference.

The official x64 installer was also downloaded and checked against SourceForge's
published SHA-256:
`7403be7427bbe1936a40dded082829b6e217fc4f5990fee5cba501f0ae055afa`.
Without installing it, its extracted Benchmark executable contains the UTF-16
version string `3.0.1 (20140527; release)`. That executable has no custom
configuration-path argument; its audio-processing path was not run against the
machine's global configuration. The old grammar was executed in the separately
built 3.0.1 probe, while the full audio reproduction used XT.

This is a compatibility regression in XT's expression dependency boundary.
Restore the old separator syntax there. SECS 2R also emits one assignment per
Eval line so its new exports work with older XT builds.

## Implementation contract

- Keep compatibility handling in `EngineParser::evaluate`, shared by Eval,
  inline expressions, conditional expressions, and Editor analysis.
- Convert semicolons outside double-quoted string literals and `#` comments
  to newlines. A comment ends at an actual LF, as in both parser versions;
  a CR alone does not end it. Preserve quotes and semicolons within comments,
  escaped quotes, escaped backslashes and all string contents.
  Each replacement keeps the character count and diagnostic offsets.
- Evaluate the whole converted expression with one `SetExpr`/`Eval` call.
  Do not run statements separately: a later syntax error must not execute
  earlier assignments. Runtime errors retain the parser's execution semantics.
- If there are no separator semicolons, preserve the existing evaluation path.
- Cover leading, trailing and repeated separators. A semicolon-only expression
  may contain spaces and LF and returns the old void result. The 3.0.1 probe
  rejects tab, CR, VT and FF in such an expression, so those are not silently
  accepted. Unrelated empty expressions and the parser's length limit retain
  their current behavior.
- Do not special-case SECS variable names, change numeric DSP, or modify
  configuration text on disk. Existing codec round trips must remain intact.
- Do not alter the third-party dependency or its version.

## Ownership and validation

The implementation worker owns `parser/EngineParser.cpp`,
`Tests/HybridConvTests/ParserTests.cpp`, and a focused real-engine regression
in the existing `Tests/EngineOrchestrationTests/ConfigLoadTests.cpp`.
Add another source file or project entry only if a concrete need appears.
The coordinator owns this design record, the four README/changelog files,
independent verification, commits, PRs and CI follow-up. Other work in the
checkout must be preserved.

First make the new regression fail on the current code. Then verify parser
results, variable values, strings, comments and escape sequences, malformed later
statements, valid newline expressions, and invalid nested separators. Use the
real FilterEngine with a SECS-shaped stereo configuration and distinct FIR
taps; inspect load errors and the complete output, not only a nonzero peak.

Build Common, Benchmark and the related test suites using the repository's
warnings-as-errors policy. Re-run original SECS files and the revised Rust
exports through the patched engine. Check 48 kHz and mismatched sample rates
as separate conditions. Run the full CI matrix before merging.

## Verification results (2026-10-21 KST)

The new parser regressions failed on the original XT code. The real-engine
stereo FIR regression reported load errors and a maximum sample error of 0.75.
After the patch, Common, Benchmark and the related suites built with the existing
warnings-as-errors settings. The coordinator independently ran:

- HybridConvTests, including 50 parser checks.
- EngineOrchestrationTests: 11,385 checks.
- AudioRegressionTests: all 32 existing reference cases passed.
- Original SECS and split-Eval control configurations: 28 channel-impulse runs,
  zero expression errors; every PCM16 output array equals the corrected
  control output from the unpatched engine.
- Actual revised SECS 2R exports: 50 channel-impulse runs across nine layouts.
  The 11 outputs shared with the original-writer fixtures match exactly.
- An independent SciPy render of the 2.1 crossover/FIR/LFE paths and a saved
  three-channel correction result: 2,359,296 samples, maximum error 0.572468
  PCM16 quantization steps, no time alignment fitted to the output.
- The stereo Rust export under a Korean path with spaces: both complete
  outputs match the ASCII-path control.

The three local test suites also reported balanced aligned allocations.
The source changes are limited to the shared parser boundary and the new
regressions; FIR coefficients, convolution processing and configuration codecs
are unchanged.

Eight separate device-format and routing controls distinguish other causes of
an unapplied correction. A 48 kHz FIR is rejected at 44.1/96 kHz. A guarded
output that is not present stays silent. SECS's compact 2.1 mapping uses channel
3 for LFE/SW1, so a six-channel device whose LFE is channel 4 needs both mapping
values changed to 4. The remapped case is filtered correctly.

The actual reporter's exported files, device layout and application version
were not available. This reproduces and fixes a concrete compatibility failure;
it does not establish which conditions occurred on that machine.
