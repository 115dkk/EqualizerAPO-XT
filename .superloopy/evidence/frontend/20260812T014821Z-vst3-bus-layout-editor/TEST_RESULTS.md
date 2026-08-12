# Candidate test results

Candidate: `befe7cf1e47caca0e15c3770e5df931cd2718b14`

| Gate | Result | Relevant coverage |
| --- | --- | --- |
| `Editor.exe --selftest-vst` | PASS, 0 failures | widget names, accessible names, focus order, keyboard edit signal, VST2 disable/tooltips, removal action, modern migration, legacy round trip, state preservation |
| `EditorLogicTests.exe` | PASS, 2,967 checks | paired bus model, explicit Auto/Auto, removal, legacy migration, explicit precedence, picker/catalog and adjacent Editor models |
| `HybridConvTests.exe` | PASS, 1,635 checks | command/runtime regression suite and deterministic VST host integration |
| `Vst3HostTests` block | PASS, 101 checks | accepted/rejected asymmetric layouts, inconsistent metadata, 4.1/5.0 semantic distinction, accepted-arrangement diagnostics |
| English skin gallery | PASS, 1,370 PNGs | 5 skins x dark/light x registered normal/hover/disabled states and fixed scenarios; expected count self-check |
| Korean studio gallery | PASS, 274 PNGs | `ko_KR` translation, accepted/rejected/disguised-VST2 long copy, dark/light and registered states; expected count self-check |
| `lrelease Editor/Editor.pro` | PASS | completed Korean VST bus strings compiled into the bundled catalog |
| `git diff --check` | PASS | changed source, tests, workflow, docs, translations, and evidence |

Local native projects used the installed MSVC v143 toolset because the
repository wrapper's v145 toolset was not available on this machine. The
repository CI keeps its v145 build gate and is tracked after push.
