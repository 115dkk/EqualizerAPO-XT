# EqualizerAPO-XT Wiki
EqualizerAPO-XT is a Windows system-wide equalizer based on [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) by Jonas Thedering. The XT fork keeps the familiar configuration workflow while adding 64-bit (double) internal processing, an uncapped convolver impulse-response length, BRIR/crossfeed-oriented MultiConvolution, Hilbert and Velvet phase/decorrelation tools, explicit VST3 main-bus layouts, one-line subwoofer routing shared with a standalone VST3 plug-in, recording devices on the same footing as playback devices, and ASIO: the same `config.txt` applied to ASIO streams through a `<driver> (EQ APO XT)` entry, and any Windows endpoint offered to ASIO applications in WASAPI exclusive mode so exclusive-mode listeners keep the EQ. Portable SIMD variants cover x64 through AVX10.1 and ARM64/NEON, while an auto-detect Velopack installer selects the right build and automatic updates keep it current.

This wiki mirrors the structure of the original Equalizer APO documentation and adapts the install and packaging sections to the XT builds.

## Pages
* **[Documentation](Documentation)** — user guide: installation, first configuration, and troubleshooting. Start here.
* **[Configuration reference](Configuration-reference)** — the configuration file format and every supported command. For advanced users.
* **[Developer documentation](Developer-documentation)** — building from source, project layout, and how an APO is registered with Windows.

## Other languages
* [한국어 (Korean)](Korean)

## License and credits
EqualizerAPO-XT is distributed under the [GNU General Public License, version 2 or later](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html), the same terms as the original Equalizer APO. The ASIO wrapper (`EqualizerAPOAsio.dll`) is built against the Steinberg ASIO SDK 2.3.4 under the SDK's GPL version 3 option, so the wrapper and the installers that ship it are distributed under GPL version 3; both texts are installed as `License.txt` and `License-gpl-3.0.txt`. ASIO is a trademark and software of Steinberg Media Technologies GmbH. The VST3 hosting layer uses the MIT-licensed Steinberg VST3 *pluginterfaces*; the VST2 interface header is an independent BSD-2 clean-room reimplementation; the `SubwooferRoutingCore` DSP library and the `EAPO XT Subwoofer Routing` VST3 plug-in are MIT-licensed. Mephistos (DCinside) diagnosed the dead VST3 panel meters, contributed the WASAPI-loopback patch the panel feed grew from, and provided the plug-in used to verify it. See the project [repository](https://github.com/115dkk/EqualizerAPO-XT) for full details.
