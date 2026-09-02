/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SkinGallery.h"
#include <numbers>
// For the two gates moved out of main.cpp (audit #275 B7): the VST
// round-trip self test and the analysis layout probe.
#include <optional>
#include <unordered_map>
#include <QBoxLayout>
#include <QDockWidget>
#include <QFileInfo>
#include <QLocale>
#include <QStyle>
#include <QTimer>
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "guis/VSTPluginFilterGUI.h"
#include "widgets/cards/VSTCardEditor.h"
#include "widgets/cards/VSTSlotFillRail.h"
#include "MainWindow.h"
#include "diagnostics/ToolbarPixelProbe.h"
#include "widgets/MainToolbarKit.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QRadioButton>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QTreeView>
#include <QUrl>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/guis/CopyFilterGUI.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/widgets/AddCardRow.h"
#include "Editor/widgets/EqGraphView.h"
#include "Editor/widgets/SegmentedControl.h"
#include "Editor/analysis/AnalysisMetric.h"
#include "Editor/analysis/AnalysisResponse.h"
#include "filters/BiQuad.h"
#include "Editor/skins/shared/SkinFileIcons.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/CommandRowFrame.h"
#include "Editor/widgets/FilterInsertSeam.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/TitleBar.h"
#include "Editor/widgets/UpdateToast.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingEditorDialog.h"
#include "Editor/widgets/cards/SubwooferRoutingCardEditor.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

namespace
{
struct GalleryRow
{
	QString name;
	QString line;
};

// Representative rows: a parametric filter, a shelf filter (three knobs in
// the legacy BiQuad GUI hosted by the card body), a peaking filter at 0 dB
// (the bipolar gain knob at its neutral detent), the preamp card (the
// bare knob + value scrub pair - the row that shows whether a skin seats
// custom widgets directly on its surface), the reference-card rows and an
// empty Copy row (the routing editor's empty state).
//
// The reference rows (Include / Convolution / MultiConvolution / VST) render
// against synthetic target files written next to a synthetic config file
// (buildReferenceFiles), so the resolved cards show their healthy named-entity
// state with a deterministic impulse-response readout. include_missing keeps
// the broken-reference transition (MISSING + Locate) in every
// skin's judged set. The VST library is intentionally unresolvable; the card
// renders its missing/not-loaded state, which doubles as the recovery-entry
// showcase. The two MultiConvolution rows cover the mapping-form card (the
// per-skin routing view over a 4-channel BRIR, both ears mapped) and the
// freshly inserted empty state; the empty one also guards the Insert path,
// where a bare "MultiConvolution:" template must still resolve to the card
// body and not fall back to an empty row. The comment and stage rows judge
// the in-place note editor and the two-lane stage card. The two device rows
// split the card's grammar over the synthetic endpoints (galleryDevices):
// "device" shows an engaged playback switch and an engaged capture well next
// to an idle endpoint (the routed/at-rest contrast every skin styles), while
// "device_all" shows the all-devices master engaged over powered-down
// endpoint chips.
// The inline-State fixture is generated through the core instead of pasting
// JSON, so the gallery always renders exactly what the codec would emit.
QString subwooferRoutingPresetRowLine()
{
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	if (!preset.succeeded())
		return QStringLiteral("SubwooferRouting:");
	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(*preset.state);
	if (!encoded.succeeded())
		return QStringLiteral("SubwooferRouting:");
	return QStringLiteral("SubwooferRouting: State ")
		+ QString::fromUtf8(encoded.text->data(),
			static_cast<int>(encoded.text->size()));
}

// Deterministic VST bus scenes exist only when the environment supplies real
// test plugins - the negotiation verdict can only be shown against a loaded
// ABI, never invented. CI builds TestVst3Plugin/TestVst2Plugin and points
// these variables at them; the VST3 fixture doubles as an upmixer when a
// copy carries the magic Upmixer.vst3 name (TestVst3Plugin's filename modes).
void appendVstBusRows(QList<GalleryRow>& rows)
{
	const QString quotedLibrary = QStringLiteral("VSTPlugin: Library \"%1\"");
	const QString vst3 = qEnvironmentVariable("EAPO_GALLERY_VST3_PLUGIN");
	if (!vst3.isEmpty() && QFileInfo::exists(vst3))
	{
		// The plain component accepts exactly stereo in / stereo out: an
		// explicit accepted contract (lamp-only verdict), the bare Auto
		// negotiation (pair verdict), and an honestly rejected 7.1 ask.
		rows.append({QStringLiteral("vst3_bus_accepted"),
			quotedLibrary.arg(vst3) + QStringLiteral(" Input Stereo Output Stereo")});
		rows.append({QStringLiteral("vst3_bus_auto"), quotedLibrary.arg(vst3)});
		rows.append({QStringLiteral("vst3_bus_rejected"),
			quotedLibrary.arg(vst3) + QStringLiteral(" Input Stereo Output 7.1")});
	}
	const QString upmixer = qEnvironmentVariable("EAPO_GALLERY_VST3_UPMIXER");
	if (!upmixer.isEmpty() && QFileInfo::exists(upmixer))
		rows.append({QStringLiteral("vst3_bus_upmix"),
			quotedLibrary.arg(upmixer) + QStringLiteral(" Input Stereo Output 7.1")});
	const QString vst2 = qEnvironmentVariable("EAPO_GALLERY_VST2_PLUGIN");
	if (!vst2.isEmpty() && QFileInfo::exists(vst2))
		rows.append({QStringLiteral("vst2_bus_ignored"),
			quotedLibrary.arg(vst2) + QStringLiteral(" Input Stereo Output 7.1")});
}

QList<GalleryRow> galleryRows()
{
	QList<GalleryRow> rows = {
		{ QStringLiteral("filter"), QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71") },
		{ QStringLiteral("shelf"), QStringLiteral("Filter 2: ON HSC Fc 8000 Hz Gain -2.5 dB Q 0.71") },
		{ QStringLiteral("gain0db"), QStringLiteral("Filter 3: ON PK Fc 1000 Hz Gain 0 dB Q 1") },
		{ QStringLiteral("preamp"), QStringLiteral("Preamp: -6 dB") },
		{ QStringLiteral("include"), QStringLiteral("Include: example.txt") },
		{ QStringLiteral("include_nested"), QStringLiteral("Include: Surround\\example.txt") },
		{ QStringLiteral("include_missing"), QStringLiteral("Include: missing.txt") },
		{ QStringLiteral("vst"), QStringLiteral("VSTPlugin: Library example.dll") },
		// The channel-fill rails on a forced contract: input rail under the
		// header, output rail under the body, expanded because both lists
		// are explicit. Deviceless, so the values render without a missing
		// verdict (no selection context to judge against).
		{ QStringLiteral("vst_slotfill"), QStringLiteral(
			  "VSTPlugin: Library example.vst3 Input 5.1 InputChannels L,R,C,-,SL,SR"
			  " Output 5.1 OutputChannels L,R,C,LFE,RL,RR") },
		{ QStringLiteral("device"), QStringLiteral("Device: Speakers Example Audio; Microphone Example Audio") },
		{ QStringLiteral("device_all"), QStringLiteral("Device: all") },
		{ QStringLiteral("channel"), QStringLiteral("Channel: L R") },
		{ QStringLiteral("comment"), QStringLiteral("# Living room preset - tuned by ear") },
		{ QStringLiteral("stage"), QStringLiteral("Stage: pre-mix post-mix") },
		{ QStringLiteral("copy_empty"), QStringLiteral("Copy:") },
		{ QStringLiteral("copy"), QStringLiteral("Copy: VC=0.5*L+0.5*R R=L") },
		{ QStringLiteral("convolution"), QStringLiteral("Convolution: example.wav") },
		{ QStringLiteral("multiconvolution"), QStringLiteral("MultiConvolution: L=0+1 R=2+3 brir.wav") },
		{ QStringLiteral("multiconvolution_empty"), QStringLiteral("MultiConvolution:") },
		// The clean-install first impression: the graphic EQ card is the first
		// thing a fresh install shows, and the two raw-text shapes (a bare
		// note line and a programmatic If command) are the rows that once
		// rendered as nothing at all.
		{ QStringLiteral("graphiceq"), QStringLiteral("GraphicEQ: 25 -4.5; 100 -2; 1000 0; 8000 3.5; 16000 1") },
		{ QStringLiteral("text"), QStringLiteral("plain note line without a command") },
		{ QStringLiteral("iftext"), QStringLiteral("If: inputChannelCount == 2") },
		// The custom-coefficient escape hatch: the IIR card states the order
		// and both coefficient rows (a 2nd-order Butterworth low-pass at fs/4,
		// a0 = 1). Appended last so the row numbers of every earlier scene
		// stay stable against the shot baseline.
		{ QStringLiteral("iir"), QStringLiteral("Filter: ON IIR Order 2 Coefficients 0.2929 0.5858 0.2929 1.0 -0.0 0.1716") },
		// A dynamic line (inline `expression` gain): the Preamp card opens in
		// dynamic mode - powered-down knob, the expression as a token in the
		// value position - instead of parsing the text as 0.0 and destroying
		// the expression on the first knob turn. Appended last (mid-list
		// insertion renumbers every following scene).
		{ QStringLiteral("dynpreamp"), QStringLiteral("Preamp: `bass + 3` dB") },
		// The all-pass card. It has no gain and its magnitude is flat, so the
		// card has to say in words what the filter does; these shots are how
		// that reads in each skin. Written as a bandwidth on purpose - the
		// spelling the editors used to lose - and appended last, because
		// inserting mid-list renumbers every following scene against the
		// stored baseline.
		{ QStringLiteral("allpass"), QStringLiteral("Filter 4: ON AP Fc 900 Hz BW Oct 1") },
		// A notch: the other gain-less biquad. The legacy row hides the gain
		// block for every type that has no gain and lets the remaining blocks
		// stretch into the space, so this row and the all-pass above it look
		// the same - which is the evidence that the all-pass row's spacing is
		// the .ui's own behaviour and not something this campaign introduced.
		{ QStringLiteral("notch"), QStringLiteral("Filter 5: ON NO Fc 800 Hz") },
		// Independent built-in phase and sparse-FIR cards. Velvet appears in
		// both time modes, plus a malformed line so the in-card repair state is
		// judged rather than dropping to raw text.
		{ QStringLiteral("hilbert"), QStringLiteral("Hilbert: Shift=SL,SR Align=L,R Direction=-90") },
		{ QStringLiteral("velvet_dynamic"), QStringLiteral("Velvet: Mode=Dynamic Amount=85% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136") },
		{ QStringLiteral("velvet_static"), QStringLiteral("Velvet: Mode=Static Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136") },
		{ QStringLiteral("velvet_invalid"), QStringLiteral("Velvet: Mode=Dynamic Length=not-a-time") },
		// The Subwoofer Routing card in its two load-bearing shapes: the built-in
		// #246 preset as an inline State (built through the core so the JSON is
		// always the canonical bytes the engine sees), and a linked profile
		// whose file is missing, which is the warning state every skin must
		// carry without dropping the card. Appended last (mid-list insertion
		// renumbers every following scene against the stored baseline).
		{ QStringLiteral("subwooferrouting"), subwooferRoutingPresetRowLine() },
		{ QStringLiteral("subwooferrouting_missing"), QStringLiteral("SubwooferRouting: Profile \"missing.swxt.json\"") },
		// The no-op delay. The engine builds no filter for it, but the card
		// must still open the knob editor - this exact line (the insert
		// template's default) used to collapse to the raw body. Appended last
		// (mid-list insertion renumbers every following scene).
		{ QStringLiteral("delay_zero"), QStringLiteral("Delay: 0 ms") }
	};
	// Fixture-gated VST bus scenes, appended last (mid-list insertion
	// renumbers every following scene against the stored baseline).
	appendVstBusRows(rows);
	return rows;
}

// Synthetic audio endpoints for the Device rows. Offscreen runners have no
// audio devices and the Device card only grows chips for enumerated
// endpoints, so without these the card renders as a lone master chip and the
// per-skin switch grammar is never judged. Three playback endpoints (one
// engaged, one idle, one without the APO - the blank hidden behind the
// reveal toggle) and one engaged capture endpoint cover the state family.
class GalleryAPOInfo : public AbstractAPOInfo
{
public:
	GalleryAPOInfo(const std::wstring& connection, const std::wstring& name, bool input, bool installed,
		unsigned channelCount = 2, unsigned long channelMask = 0x3)
		: connection(connection), name(name), input(input), installed(installed),
		channelCount(channelCount), channelMask(channelMask)
	{
	}

	std::wstring getConnectionName() const override
	{
		return connection;
	}

	std::wstring getDeviceName() const override
	{
		return name;
	}

	std::wstring getDeviceGuid() const override
	{
		return L"";
	}

	// The card pre-selects a chip when the row's pattern matches this string
	// (DeviceCommand::matches) and serializes selections back as these exact
	// strings joined with "; " - keep them plain words so the gallery line
	// round-trips byte-identically.
	std::wstring getDeviceString() const override
	{
		return connection + L" " + name;
	}

	unsigned getChannelCount() const override
	{
		return channelCount;
	}

	unsigned getSampleRate() const override
	{
		return 48000;
	}

	unsigned long getChannelMask() const override
	{
		return channelMask;
	}

	bool isInput() const override
	{
		return input;
	}

	bool isInstalled() const override
	{
		return installed;
	}

	bool canBeUpgraded() const override
	{
		return false;
	}

	bool hasChanges() const override
	{
		return false;
	}

	bool isEnhancementsDisabled() const override
	{
		return false;
	}

	bool isDefaultDevice() const override
	{
		return false;
	}

	bool isDisabled() const override
	{
		return false;
	}

	bool isUnplugged() const override
	{
		return false;
	}

	void install() override
	{
	}

	void uninstall() override
	{
	}

	void reinstall() override
	{
	}

private:
	std::wstring connection;
	std::wstring name;
	bool input;
	bool installed;
	unsigned channelCount;
	unsigned long channelMask;
};

void galleryDevices(QList<std::shared_ptr<AbstractAPOInfo>>& outputs, QList<std::shared_ptr<AbstractAPOInfo>>& inputs)
{
	outputs.append(std::make_shared<GalleryAPOInfo>(L"Speakers", L"Example Audio", false, true));
	outputs.append(std::make_shared<GalleryAPOInfo>(L"Headphones", L"Example Audio", false, true));
	outputs.append(std::make_shared<GalleryAPOInfo>(L"Digital Output", L"Example Audio", false, false));
	inputs.append(std::make_shared<GalleryAPOInfo>(L"Microphone", L"Example Audio", true, true));
}

// A canonical 16-bit PCM WAV of silence: enough for libsndfile to report the
// deterministic length / rate / channel readout the convolution cards print.
bool writeWavFile(const QString& path, quint16 channels, quint32 sampleRate, quint32 frames)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint16 bitsPerSample = 16;
	const quint16 blockAlign = channels * bitsPerSample / 8;
	const quint32 dataSize = frames * blockAlign;
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataSize);
	out.writeRawData("WAVE", 4);
	out.writeRawData("fmt ", 4);
	out << quint32(16) << quint16(1) << channels << sampleRate
		<< quint32(sampleRate * blockAlign) << blockAlign << bitsPerSample;
	out.writeRawData("data", 4);
	out << dataSize;
	const QByteArray silence(dataSize, '\0');
	return out.writeRawData(silence.constData(), silence.size()) == silence.size();
}

// Synthetic reference targets for the gallery rows, next to a synthetic
// config file so relative references resolve: example.txt (Include),
// example.wav (Convolution, 100 ms mono) and brir.wav (MultiConvolution,
// 100 ms 4-channel - the "4 ch" readout and the L=0+1 R=2+3 routing view).
// missing.txt is deliberately absent. Returns the config path setLines gets,
// or an empty string on failure.
QString buildReferenceFiles(const QDir& outDir)
{
	QDir refsDir(outDir.filePath(QStringLiteral("refs")));
	if (!refsDir.mkpath(QStringLiteral(".")))
		return QString();

	QFile include(refsDir.filePath(QStringLiteral("example.txt")));
	if (!include.open(QIODevice::WriteOnly))
		return QString();
	include.write("# gallery include target\n");
	include.close();

	// A nested target so the location line (secondary metadata under the
	// name) and its per-skin treatment appear in the judged set.
	if (!refsDir.mkpath(QStringLiteral("Surround")))
		return QString();
	QFile nested(refsDir.filePath(QStringLiteral("Surround/example.txt")));
	if (!nested.open(QIODevice::WriteOnly))
		return QString();
	nested.write("# gallery nested include target\n");
	nested.close();

	if (!writeWavFile(refsDir.filePath(QStringLiteral("example.wav")), 1, 48000, 4800))
		return QString();
	if (!writeWavFile(refsDir.filePath(QStringLiteral("brir.wav")), 4, 48000, 4800))
		return QString();

	QFile config(refsDir.filePath(QStringLiteral("gallery.txt")));
	if (!config.open(QIODevice::WriteOnly))
		return QString();
	config.write("# gallery config anchor - references resolve relative to this file\n");
	config.close();
	return refsDir.filePath(QStringLiteral("gallery.txt"));
}

// Pin a file or directory's timestamps to a fixed instant so the file
// dialog's Detail date column is identical between runs on the same machine.
// Directories need the Win32 path: QFile::setFileTime only opens files.
bool pinFileTime(const QString& path)
{
	HANDLE handle = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), FILE_WRITE_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	SYSTEMTIME systemTime = {};
	systemTime.wYear = 2026;
	systemTime.wMonth = 1;
	systemTime.wDay = 1;
	systemTime.wHour = 12;
	FILETIME fileTime;
	SystemTimeToFileTime(&systemTime, &fileTime);
	const bool ok = SetFileTime(handle, &fileTime, &fileTime, &fileTime) != 0;
	CloseHandle(handle);
	return ok;
}

// Fixture tree for the file-dialog chrome shot: two folders and two matching
// configurations, every entry's timestamps pinned so the Detail view's
// name/size/date cells do not depend on when the gallery ran. The dialog's
// "Look in" text still embeds the output dir, like the reference-path shots.
// Returns the directory the dialog opens on, or an empty string on failure.
QString buildFileDialogFixture(const QDir& outDir)
{
	QDir fixtureDir(outDir.filePath(QStringLiteral("filedialog")));
	if (!fixtureDir.mkpath(QStringLiteral("config")) || !fixtureDir.mkpath(QStringLiteral("IRs")))
		return QString();

	const auto writeText = [&fixtureDir](const QString& name, const QByteArray& content)
	{
		QFile file(fixtureDir.filePath(name));
		if (!file.open(QIODevice::WriteOnly))
			return false;
		file.write(content);
		file.close();
		return pinFileTime(fixtureDir.filePath(name));
	};
	if (!writeText(QStringLiteral("demo.txt"), QByteArrayLiteral("# gallery demo config\nPreamp: -6 dB\n")))
		return QString();
	if (!writeText(QStringLiteral("voice - bass boost.txt"), QByteArrayLiteral("# gallery demo config\nPreamp: -3 dB\n")))
		return QString();
	if (!writeWavFile(fixtureDir.filePath(QStringLiteral("IRs/room.wav")), 2, 48000, 4800))
		return QString();
	if (!pinFileTime(fixtureDir.filePath(QStringLiteral("IRs/room.wav"))))
		return QString();
	for (const QString& dir : { QStringLiteral("config"), QStringLiteral("IRs") })
		if (!pinFileTime(fixtureDir.filePath(dir)))
			return QString();
	if (!pinFileTime(fixtureDir.absolutePath()))
		return QString();
	return fixtureDir.absolutePath();
}

// renderSkin() renders, per skin and per mode: every gallery row in
// kStatesPerRow states (normal + hover from renderStates(commented=false), and
// disabled from renderStates(commented=true)), plus the registered fixed
// chrome shots (picker x3, toolbar, titlebar, menubar, menu, analysis,
// addrow x2, seam, toast, controls, filedialog, graph x2, copyfold x5,
// multiconvfold x2, logic, channelscope). run() multiplies these by skins x 2
// modes to self-check the
// output count, so adding a gallery row needs no external count to be
// updated. The fixed count is derived from galleryScenarios().
constexpr int kStatesPerRow = 3;

struct GalleryScenario
{
	QString id;
	QStringList states;
};

// Fixed chrome scenarios are registered once. The render blocks below build
// their specialized widgets, while this table owns scenario identity, state
// vocabulary, and the deterministic shot-count contract.
const QList<GalleryScenario>& galleryScenarios()
{
	static const QList<GalleryScenario> scenarios = {
		{ QStringLiteral("picker"), { QStringLiteral("normal"), QStringLiteral("hover"),
			QStringLiteral("empty"), QStringLiteral("phasetime") } },
		{ QStringLiteral("srdialog"), { QStringLiteral("default"),
			QStringLiteral("preset"), QStringLiteral("expanded") } },
		{ QStringLiteral("toolbar"), { QStringLiteral("normal") } },
		{ QStringLiteral("analysis"), { QStringLiteral("normal") } },
		{ QStringLiteral("titlebar"), { QStringLiteral("normal") } },
		{ QStringLiteral("menubar"), { QStringLiteral("normal") } },
		{ QStringLiteral("menu"), { QStringLiteral("normal") } },
		{ QStringLiteral("addrow"), { QStringLiteral("normal"), QStringLiteral("hover") } },
		{ QStringLiteral("seam"), { QStringLiteral("hover") } },
		{ QStringLiteral("toast"), { QStringLiteral("normal") } },
		{ QStringLiteral("filedialog"), { QStringLiteral("normal") } },
		{ QStringLiteral("graph"), { QStringLiteral("normal"), QStringLiteral("cursor"),
			QStringLiteral("phase"), QStringLiteral("groupdelay") } },
		{ QStringLiteral("segment"), { QStringLiteral("normal"), QStringLiteral("selected"),
			QStringLiteral("hover") } },
		{ QStringLiteral("copyfold"), { QStringLiteral("normal"), QStringLiteral("empty"),
			QStringLiteral("expanded"), QStringLiteral("editor"), QStringLiteral("sourceeditor") } },
		{ QStringLiteral("multiconvfold"), { QStringLiteral("normal"),
			QStringLiteral("expanded") } },
		{ QStringLiteral("logic"), { QStringLiteral("normal") } },
		{ QStringLiteral("channelscope"), { QStringLiteral("normal") } },
		{ QStringLiteral("velvet-advanced"), { QStringLiteral("normal") } },
		{ QStringLiteral("velvet-narrow"), { QStringLiteral("normal") } },
		{ QStringLiteral("controls"), { QStringLiteral("normal") } }
	};
	return scenarios;
}

int fixedScenarioShotCount()
{
	int count = 0;
	for (const GalleryScenario& scenario : galleryScenarios())
		count += scenario.states.size();
	return count;
}

// A rendered toolbar is never one flat colour: buttons, combos and labels
// cover a sizable share of the strip. A grab that matches its own corner
// pixel almost everywhere means the controls were not painted - the
// styled-background overlay regression (a full-size chrome overlay child
// picking up the sheets' universal QWidget background rule and blanking
// the whole strip) produced exactly this while every visibility flag,
// geometry and child list stayed healthy.
// Faithful chrome replica of MainWindow's toolbar: same object names, same
// widget train, dummy data where the real one reads devices. The gallery
// judges chrome, not data, and constructing the real toolbar would drag in
// device enumeration (flaky on machines without audio endpoints).
QToolBar* buildToolbarReplica(QWidget* parent)
{
	QToolBar* toolBar = new QToolBar(parent);
	MainToolbarKit::Content content;
	content.instantMode = QStringLiteral("Instant mode");
	content.saved = QStringLiteral("Saved");
	content.device = QStringLiteral("Device");
	content.channels = QStringLiteral("Channels");
	content.deviceValue = QStringLiteral("Default (Speakers - Example Audio)");
	content.channelValue = QStringLiteral("7.1 surround");
	content.formatText = QStringLiteral("Passthrough");
	content.formatSeverity = QStringLiteral("warning");
	content.formatVisible = true;
	MainToolbarKit::populate(toolBar, content, true);
	SkinManager::instance()->styleMainToolbar(toolBar);
	return toolBar;
}

std::shared_ptr<const AnalysisResponse> galleryAnalysisResponse();

QWidget* buildAnalysisPanelReplica(QWidget* parent)
{
	QWidget* panel = new QWidget(parent);
	QHBoxLayout* dockLayout = new QHBoxLayout(panel);
	dockLayout->setContentsMargins(10, 6, 10, 10);
	dockLayout->setSpacing(8);

	QFrame* bar = new QFrame;
	bar->setObjectName(QStringLiteral("analysisControlBar"));
	bar->setAttribute(Qt::WA_StyledBackground, true);
	bar->setMaximumWidth(280);
	QGridLayout* grid = new QGridLayout(bar);
	// Matches MainWindow.ui after the metric switch and the base-delay option
	// joined this bar: the two extra rows are paid for by tightening the
	// rhythm and by pairing the four readouts two to a row, so the row count
	// stays at nine; the cap grew with the type scale (250 -> 280px).
	grid->setContentsMargins(10, 6, 18, 6);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(4);
	grid->setColumnStretch(1, 1);

	const QStringList formLabels = { QStringLiteral("From"), QStringLiteral("Channel"),
		QStringLiteral("Res"), QStringLiteral("Pos") };
	const QStringList formValues = { QStringLiteral("config.txt"), QStringLiteral("L"),
		QString(), QStringLiteral("Bottom") };
	for (int row = 0; row < formLabels.size(); row++)
	{
		QLabel* label = new QLabel(formLabels[row]);
		label->setObjectName(QStringLiteral("AnalysisFormLabel"));
		grid->addWidget(label, row, 0);
		if (formValues[row].isEmpty())
		{
			// The resolution field: MainWindow's ExponentialSpinBox paints as a
			// plain QSpinBox (only the stepping differs), so the replica keeps
			// the lighter widget under the same object name.
			QSpinBox* spin = new QSpinBox;
			spin->setObjectName(QStringLiteral("AnalysisFormSpin"));
			spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			spin->setRange(128, 8388608);
			spin->setValue(65536);
			grid->addWidget(spin, row, 1);
		}
		else
		{
			QComboBox* combo = new QComboBox;
			combo->setObjectName(QStringLiteral("AnalysisFormCombo"));
			combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
			combo->setMinimumContentsLength(6);
			combo->addItem(formValues[row]);
			grid->addWidget(combo, row, 1);
		}
	}

	SegmentedControl* metricSegment = new SegmentedControl;
	metricSegment->setLabels({ QStringLiteral("Mag"), QStringLiteral("Phase"), QStringLiteral("GD") });
	grid->addWidget(metricSegment, 4, 0, 1, 2);

	QCheckBox* includeBaseDelay = new QCheckBox(QStringLiteral("Include base delay"));
	includeBaseDelay->setObjectName(QStringLiteral("AnalysisFormCheck"));
	grid->addWidget(includeBaseDelay, 5, 0, 1, 2);

	const QStringList statLabels = { QStringLiteral("Peak"), QStringLiteral("Lat"),
		QStringLiteral("Init"), QStringLiteral("CPU") };
	const QStringList statValues = { QStringLiteral("-6.0 dB"), QStringLiteral("0.0 ms"),
		QStringLiteral("0.4 ms"), QStringLiteral("0.1 %") };
	for (int i = 0; i < statLabels.size(); i++)
	{
		QFrame* chipFrame = new QFrame;
		chipFrame->setObjectName(QStringLiteral("AnalysisStatChip"));
		chipFrame->setAttribute(Qt::WA_StyledBackground, true);
		QHBoxLayout* chipLayout = new QHBoxLayout(chipFrame);
		chipLayout->setContentsMargins(8, 3, 8, 3);
		chipLayout->setSpacing(6);
		QLabel* label = new QLabel(statLabels[i]);
		label->setObjectName(QStringLiteral("AnalysisStatLabel"));
		chipLayout->addWidget(label);
		QLabel* value = new QLabel(statValues[i]);
		value->setObjectName(QStringLiteral("AnalysisStatValue"));
		value->setProperty("severity", QStringLiteral("normal"));
		chipLayout->addWidget(value);

		const int row = 6 + i / 2;
		QHBoxLayout* pairLayout = qobject_cast<QHBoxLayout*>(
			grid->itemAtPosition(row, 0) == nullptr ? nullptr : grid->itemAtPosition(row, 0)->layout());
		if (pairLayout == nullptr)
		{
			pairLayout = new QHBoxLayout;
			pairLayout->setSpacing(6);
			grid->addLayout(pairLayout, row, 0, 1, 2);
		}
		pairLayout->addWidget(chipFrame);
	}
	grid->setRowStretch(8, 1);

	EqGraphView* graph = new EqGraphView(panel);
	graph->setObjectName(QStringLiteral("ModernAnalysisGraph"));
	// Fed the same synthetic spectrum as the standalone graph shots. Left
	// empty, this replica used to draw a flat line across the middle, because
	// a response with no data read as a perfectly flat 0 dB one - a measurement
	// the analyzer had never taken. An unanalyzed graph now draws no trace at
	// all, which is correct and which would make this shot an empty pane.
	graph->setResponse(galleryAnalysisResponse(), QStringLiteral("All"));
	dockLayout->addWidget(bar);
	dockLayout->addWidget(graph, 1);
	return panel;
}

// The gallery's analysis fixture, as a complex spectrum.
//
// The graph consumes what the analyzer produces - a complex response per FFT bin
// - so the fixture has to be one too, not a hand-drawn dB curve. These are the
// same nine breakpoints the fixture has always used (boosts, cuts and a
// clipping shelf so the over-0dB emphasis shows); the curve between them is
// straight in dB against log frequency, and each bin simply samples it. Phase
// is left at zero: this fixture exists for the magnitude shots, and a metric
// that needs phase gets its own fixture.
std::shared_ptr<const AnalysisResponse> galleryAnalysisResponse()
{
	struct Breakpoint { double hz; double db; };
	static const Breakpoint curve[] = {
		{20.0, 0.0}, {45.0, 5.5}, {120.0, 2.0}, {300.0, -4.5}, {900.0, 1.0},
		{2500.0, -7.5}, {6000.0, 3.0}, {11000.0, 6.5}, {20000.0, -2.0}
	};
	constexpr int breakpointCount = int(sizeof(curve) / sizeof(curve[0]));

	const auto dbAt = [&](double hz) {
		if (hz <= curve[0].hz)
			return curve[0].db;
		if (hz >= curve[breakpointCount - 1].hz)
			return curve[breakpointCount - 1].db;
		for (int i = 1; i < breakpointCount; i++)
		{
			if (hz > curve[i].hz)
				continue;
			const double t = std::log(hz / curve[i - 1].hz) / std::log(curve[i].hz / curve[i - 1].hz);
			return curve[i - 1].db + t * (curve[i].db - curve[i - 1].db);
		}
		return curve[breakpointCount - 1].db;
	};

	auto response = std::make_shared<AnalysisResponse>();
	response->sampleRate = 48000;
	response->fftSize = 65536;
	const size_t binCount = AnalysisResponse::binCountFor(response->fftSize);
	response->bins.resize(binCount);
	for (size_t i = 0; i < binCount; i++)
	{
		const double hz = response->frequencyOf(i);
		response->bins[i] = std::complex<double>(std::pow(10.0, dbAt(hz) / 20.0), 0.0);
	}
	return response;
}

// A real 2nd-order all-pass, evaluated on the unit circle from the engine's own
// coefficients. Flat in magnitude by construction, so it is the fixture that
// makes the phase and group-delay shots show something a magnitude plot cannot:
// a full turn of phase around 1 kHz and a delay peak sitting on it.
std::shared_ptr<const AnalysisResponse> galleryAllPassResponse()
{
	auto response = std::make_shared<AnalysisResponse>();
	response->sampleRate = 48000;
	response->fftSize = 65536;
	response->bins.resize(AnalysisResponse::binCountFor(response->fftSize));

	BiQuad biquad(BiQuad::ALL_PASS, 0.0, 1000.0, response->sampleRate, 0.707, false);
	double packed[4];
	double b0 = 0.0;
	biquad.getCoefficients(packed, b0);
	for (size_t i = 0; i < response->bins.size(); i++)
	{
		const double omega = 2.0 * std::numbers::pi_v<double> * response->frequencyOf(i) / response->sampleRate;
		const std::complex<double> z1 = std::polar(1.0, -omega);
		const std::complex<double> z2 = z1 * z1;
		response->bins[i] = (b0 + packed[0] * z1 + packed[1] * z2)
			/ (1.0 + packed[2] * z1 + packed[3] * z2);
	}
	return response;
}

// QSS :hover matches widgets whose Qt::WA_UnderMouse attribute is set, and
// custom paint code reads the same attribute via underMouse(). Setting it
// manually lets the offscreen renderer capture the hover look without a real
// cursor. Pseudo-states are evaluated at paint time, so update() suffices.
void setHoverEquivalent(QWidget* root, bool on)
{
	root->setAttribute(Qt::WA_UnderMouse, on);
	for (QWidget* child : root->findChildren<QWidget*>())
		child->setAttribute(Qt::WA_UnderMouse, on);
	root->update();
}

// Overflow gate: a row must fit its 960px viewport in every skin. A visible
// horizontal scrollbar inside the row is the overflow defect this guards;
// failing the render makes CI keep the broken shot as evidence instead of
// shipping it silently.
int assertNoHorizontalScrollBar(QWidget* row, const QString& skinId, const QString& mode,
	const QString& rowName, const QString& state)
{
	for (const QScrollBar* bar : row->findChildren<QScrollBar*>())
	{
		if (bar->orientation() == Qt::Horizontal && bar->isVisible())
		{
			// For a scroll bar, pageStep is the viewport width and maximum the
			// hidden remainder, so content = maximum + pageStep.
			qWarning("SkinGallery: horizontal scrollbar in row %s_%s_%s_%s (content %d overflows viewport %d)",
				qPrintable(skinId), qPrintable(mode), qPrintable(rowName), qPrintable(state),
				bar->maximum() + bar->pageStep(), bar->pageStep());
			return 1;
		}
	}
	return 0;
}

bool saveGrab(QWidget* row, const QDir& outDir, const QString& skinId, const QString& mode,
	const QString& rowName, const QString& state)
{
	const QString fileName = QStringLiteral("%1_%2_%3_%4.png").arg(skinId, mode, rowName, state);
	QPixmap pixmap = row->grab();
	if (pixmap.isNull())
	{
		qWarning("SkinGallery: grab failed for %s", qPrintable(fileName));
		return false;
	}
	if (!pixmap.save(outDir.filePath(fileName), "PNG"))
	{
		qWarning("SkinGallery: could not write %s", qPrintable(fileName));
		return false;
	}
	return true;
}

// Build a fresh FilterTable holding the given lines and return the card row
// widgets in line order. The table must be built after applySkin so every row
// is polished once against the active stylesheet, mirroring the real skin
// switch flow (clearRows + updateGuis).
QList<FilterCardRow*> buildRows(QScrollArea& scrollArea, const QString& configPath, const QList<QString>& lines,
	std::shared_ptr<AbstractAPOInfo> device = nullptr, unsigned long channelMask = 0)
{
	// Mirror MainWindow's hosting: widgetResizable makes the scroll area drive
	// the table's width, which FilterCardRow::sizeHint reads back through
	// getPreferredWidth(). Without it the table never gets a real size and
	// every row collapses to a few pixels.
	scrollArea.setWidgetResizable(true);
	FilterTable* table = new FilterTable();
	if (qEnvironmentVariableIsSet("EAPO_GALLERY_LEGACY"))
		table->setRenderMode(FilterTable::LegacyRows);
	scrollArea.setWidget(table);
	QList<std::shared_ptr<AbstractAPOInfo>> outputDevices, inputDevices;
	galleryDevices(outputDevices, inputDevices);
	// The card path renders deviceless by default (its editors derive their
	// ports from the command text); a caller that judges device-channel
	// seeding (the Copy fold scenes) passes its own synthetic endpoint. The
	// heritage dump selects a synthetic device likewise: the legacy
	// CopyFilterGUI scene only populates through configureChannels(), which
	// is empty without one.
	if (device != nullptr)
		table->updateDeviceAndChannelMask(device, channelMask);
	else if (qEnvironmentVariableIsSet("EAPO_GALLERY_LEGACY") && !outputDevices.isEmpty())
		table->updateDeviceAndChannelMask(outputDevices.first(), 0);
	else
		table->updateDeviceAndChannelMask(nullptr, 0);
	table->initialize(&scrollArea, outputDevices, inputDevices);
	// The config path anchors relative reference resolution to the synthetic
	// target files (buildReferenceFiles) and namespaces per-file row prefs in
	// the registry; the gallery only reads prefs, never saves them.
	table->setLines(configPath, lines);
	table->updateGuis();
	scrollArea.show();
	// Flush the posted polish/layout events, then force the grid to assign row
	// geometry before grabbing.
	QApplication::processEvents();
	if (table->layout() != nullptr)
		table->layout()->activate();
	QApplication::processEvents();
	return table->findChildren<FilterCardRow*>(QString(), Qt::FindDirectChildrenOnly);
}

int renderStates(const QDir& outDir, const QString& skinId, const QString& mode,
	const QString& configPath, const QList<GalleryRow>& rows, bool commented)
{
	QList<QString> lines;
	for (const GalleryRow& row : rows)
		lines.append(commented ? QStringLiteral("# ") + row.line : row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	QList<FilterCardRow*> rowWidgets = buildRows(scrollArea, configPath, lines);
	if (rowWidgets.size() != rows.size())
	{
		qWarning("SkinGallery: expected %lld rows, got %lld (%s %s)",
			static_cast<long long>(rows.size()), static_cast<long long>(rowWidgets.size()),
			qPrintable(skinId), qPrintable(mode));
		return 1;
	}

	int failures = 0;
	for (int i = 0; i < rowWidgets.size(); i++)
	{
		FilterCardRow* row = rowWidgets[i];
		if (commented)
		{
			// A commented-out line is the product's real disabled state: power
			// toggle off, body editor disabled, muted chrome.
			failures += assertNoHorizontalScrollBar(row, skinId, mode, rows[i].name, QStringLiteral("disabled"));
			failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("disabled")) ? 0 : 1;
			continue;
		}

		failures += assertNoHorizontalScrollBar(row, skinId, mode, rows[i].name, QStringLiteral("normal"));
		failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("normal")) ? 0 : 1;
		setHoverEquivalent(row, true);
		failures += assertNoHorizontalScrollBar(row, skinId, mode, rows[i].name, QStringLiteral("hover"));
		failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("hover")) ? 0 : 1;
		setHoverEquivalent(row, false);
	}
	return failures;
}

int renderSkin(const QDir& outDir, const QString& skinId, const QString& configPath, bool dark)
{
	SkinManager::instance()->applySkin(skinId, dark);
	if (SkinManager::instance()->currentSkinId() != skinId)
	{
		// Skins::byId silently falls back to studio for unknown ids; a typo in
		// --skin-gallery-skins must fail loudly instead of producing duplicate
		// studio shots under a wrong name.
		qWarning("SkinGallery: unknown skin id '%s'", qPrintable(skinId));
		return 1;
	}

	const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
	int failures = 0;
	failures += renderStates(outDir, skinId, mode, configPath, galleryRows(), false);
	failures += renderStates(outDir, skinId, mode, configPath, galleryRows(), true);

	// The skin's "add filter" picker with the real template set, captured the
	// same way the rows are. A throwaway FilterTable supplies the entries from
	// the same command catalog chooseFilterTemplate consults at runtime.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, { QStringLiteral("Preamp: -6 dB") });
		const FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		FilterPickerView* picker = SkinManager::instance()->createFilterPicker(nullptr);
		picker->setEntries(table != nullptr ? table->filterPickerEntries() : QList<FilterPickerEntry>());
		picker->adjustSize();
		picker->show();
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("normal")) ? 0 : 1;
		// Showcase states. Pickers that have not implemented a state render
		// their normal look (base no-op), so the shot count stays fixed.
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::HoverFirstEntry);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("hover")) ? 0 : 1;
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::EmptySearch);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("empty")) ? 0 : 1;
		// The Phase & Time group. Without this the gallery only shows that the
		// all-pass left the parametric list, not where it went - and the list
		// is taller than the picker, so the group is below the fold in the
		// resting shot.
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::PhaseAndTimeSearch);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("phasetime")) ? 0 : 1;
		delete picker;
	}

	// The full subwoofer-routing editor dialog, in the two states a user meets
	// first: the seeded default for a stereo+LFE endpoint, and the built-in
	// #246 preset. The dialog hosts the skin's routing renderer twice plus
	// the response view, so it is judged per skin like the picker.
	{
		const subroute::PresetCreateResult preset =
			subroute::createBuiltInPreset(
				subroute::kIssue246FrontRear41PresetId);
		const struct
		{
			QString state;
			subroute::SubwooferRoutingState value;
			bool expandRouting = false;
		} dialogStates[] = {
			{ QStringLiteral("default"),
				subwooferroutingeditor::buildDefaultState(
					{ L"L", L"R", L"LFE" }), false },
			{ QStringLiteral("preset"),
				preset.succeeded()
					? *preset.state
					: subwooferroutingeditor::buildDefaultState(
						{ L"L", L"R", L"LFE" }), false },
			{ QStringLiteral("expanded"),
				preset.succeeded()
					? *preset.state
					: subwooferroutingeditor::buildDefaultState(
						{ L"L", L"R", L"LFE" }), true }
		};
		if (!preset.succeeded())
		{
			qWarning("SkinGallery: built-in subwoofer-routing preset failed "
				"to instantiate; the srdialog preset shot falls back to "
				"the default state");
			failures++;
		}
		for (const auto& dialogState : dialogStates)
		{
			SubwooferRoutingEditorDialog dialog(dialogState.value, 48000);
			dialog.resize(1360, 810);
			dialog.show();
			QApplication::processEvents();

			const QWidget* presetFocus = dialog.findChild<QWidget*>(
				QStringLiteral("SubwooferRoutingPresetCombo"));
			if (presetFocus == nullptr || dialog.focusWidget() != presetFocus)
			{
				qWarning("SkinGallery: subwoofer-routing dialog did not start "
					"on the preset editor (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures++;
			}

			const QList<RoutingView*> routingViews =
				dialog.findChildren<RoutingView*>();
			if (dialogState.expandRouting)
			{
				for (RoutingView* view : routingViews)
					view->galleryShowcase(QStringLiteral("expanded"));
				QApplication::processEvents();

				QScrollArea* contentScroll = dialog.findChild<QScrollArea*>(
					QStringLiteral("SubwooferRoutingContentScroll"));
				QWidget* buttons = dialog.findChild<QWidget*>(
					QStringLiteral("SubwooferRoutingButtonBox"));
				const bool needsVerticalScroll =
					skinId != QStringLiteral("studio");
				if (contentScroll == nullptr
					|| (needsVerticalScroll
						&& contentScroll->verticalScrollBar()->maximum() <= 0)
					|| buttons == nullptr || !buttons->isVisibleTo(&dialog)
					|| buttons->geometry().bottom()
						> dialog.contentsRect().bottom())
				{
					qWarning("SkinGallery: expanded subwoofer-routing dialog "
						"did not contain overflow or keep its actions visible "
						"(%s %s)", qPrintable(skinId), qPrintable(mode));
					failures++;
				}
			}
			failures += saveGrab(&dialog, outDir, skinId, mode,
				QStringLiteral("srdialog"), dialogState.state) ? 0 : 1;

			// Copy's add-channel field commits with Enter. Exercise the real
			// inline editor after the screenshot and prove the same key does not
			// accept the containing dialog.
			if (!routingViews.isEmpty())
			{
				routingViews.front()->galleryShowcase(
					QStringLiteral("addChannel"));
				QApplication::processEvents();
				QPointer<QLineEdit> inlineEditor =
					qobject_cast<QLineEdit*>(QApplication::focusWidget());
				if (inlineEditor == nullptr)
				{
					qWarning("SkinGallery: Copy add-channel editor did not "
						"take focus (%s %s)",
						qPrintable(skinId), qPrintable(mode));
					failures++;
				}
				else
				{
					QKeyEvent enterPress(QEvent::KeyPress,
						Qt::Key_Return, Qt::NoModifier);
					QApplication::sendEvent(inlineEditor, &enterPress);
					QApplication::processEvents();
				}
				if (!dialog.isVisible()
					|| dialog.result() == QDialog::Accepted)
				{
					qWarning("SkinGallery: Copy add-channel Enter accepted "
						"the subwoofer-routing dialog (%s %s)",
						qPrintable(skinId), qPrintable(mode));
					failures++;
				}
			}
			dialog.close();
			QApplication::processEvents();
		}
	}

	// The skin's main-toolbar chrome on a faithful replica (same object names
	// and widget train as MainWindow, dummy device data).
	{
		QToolBar* toolBar = buildToolbarReplica(nullptr);
		toolBar->resize(960, toolBar->sizeHint().height());
		toolBar->show();
		QApplication::processEvents();
		failures += saveGrab(toolBar, outDir, skinId, mode, QStringLiteral("toolbar"), QStringLiteral("normal")) ? 0 : 1;
		delete toolBar;
	}

	// The analysis dock's settings cell beside the graph, judged per skin
	// like the toolbar.
	{
		QWidget* panel = buildAnalysisPanelReplica(nullptr);
		panel->resize(960, 300);
		panel->show();
		QApplication::processEvents();
		failures += saveGrab(panel, outDir, skinId, mode, QStringLiteral("analysis"), QStringLiteral("normal")) ? 0 : 1;
		delete panel;
	}

	// Window chrome: the custom title bar over a dummy host. The Korean text
	// in the title is deliberate - it makes Hangul clipping/shaping defects
	// (reported from the field as "설정" rendering like "ㅅ정") visible in the
	// gallery on every machine, including CI.
	{
		QWidget host;
		host.setWindowTitle(QStringLiteral("Equalizer APO Configuration Editor — 설정.txt"));
		TitleBar* bar = new TitleBar(&host, nullptr);
		bar->resize(960, bar->sizeHint().height());
		bar->show();
		QApplication::processEvents();
		failures += saveGrab(bar, outDir, skinId, mode, QStringLiteral("titlebar"), QStringLiteral("normal")) ? 0 : 1;
		delete bar;
	}

	// Menu bar replica with the real top-level titles plus a Korean sample.
	{
		QMenuBar* menuBar = new QMenuBar(nullptr);
		menuBar->setObjectName(QStringLiteral("GalleryMenuBar"));
		menuBar->addMenu(QStringLiteral("File"));
		menuBar->addMenu(QStringLiteral("Edit"));
		menuBar->addMenu(QStringLiteral("View"));
		menuBar->addMenu(QStringLiteral("Settings"));
		menuBar->addMenu(QStringLiteral("설정"));
		menuBar->resize(960, menuBar->sizeHint().height());
		menuBar->show();
		QApplication::processEvents();
		failures += saveGrab(menuBar, outDir, skinId, mode, QStringLiteral("menubar"), QStringLiteral("normal")) ? 0 : 1;
		delete menuBar;
	}

	// An open dropdown menu with representative content: modern tinted icons,
	// a checkable item, a separator, a disabled item and a Korean label.
	{
		const QColor ink(SkinManager::instance()->tokens().text);
		QMenu* menu = new QMenu();
		menu->setObjectName(QStringLiteral("GalleryMenu"));
		menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/cut.svg"), ink, 18), QStringLiteral("Cut"));
		menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/copy.svg"), ink, 18), QStringLiteral("Copy"));
		menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/paste.svg"), ink, 18), QStringLiteral("Paste"));
		menu->addSeparator();
		QAction* checkable = menu->addAction(QStringLiteral("설정 항목 (Instant mode)"));
		checkable->setCheckable(true);
		checkable->setChecked(true);
		QAction* disabled = menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/trash.svg"), ink, 18), QStringLiteral("Delete"));
		disabled->setEnabled(false);
		menu->show();
		QApplication::processEvents();
		failures += saveGrab(menu, outDir, skinId, mode, QStringLiteral("menu"), QStringLiteral("normal")) ? 0 : 1;
		delete menu;
	}

	// List-level insertion chrome (shared insertion contract,
	// docs/skins/README.md), judged per skin like the toolbar: the trailing
	// add row at rest and under the cursor, and the first-boundary seam in
	// its hover reveal (at rest it deliberately paints nothing, so a rest
	// shot would only ever be a blank strip).
	{
		AddCardRow addRow;
		addRow.resize(960, addRow.sizeHint().height());
		addRow.show();
		QApplication::processEvents();
		// The offscreen platform parks the cursor at (0,0), which lands inside
		// this window and delivers a synthetic Enter on show, and window
		// activation hands the row keyboard focus - both dress the "normal"
		// shot as hover+focus. Clear both so the at-rest state is what gets
		// judged.
		QEvent addRowLeave(QEvent::Leave);
		QApplication::sendEvent(&addRow, &addRowLeave);
		addRow.clearFocus();
		QApplication::processEvents();
		failures += saveGrab(&addRow, outDir, skinId, mode, QStringLiteral("addrow"), QStringLiteral("normal")) ? 0 : 1;
		QEnterEvent addRowEnter(QPointF(480, 10), QPointF(480, 10), QPointF(480, 10));
		QApplication::sendEvent(&addRow, &addRowEnter);
		QApplication::processEvents();
		failures += saveGrab(&addRow, outDir, skinId, mode, QStringLiteral("addrow"), QStringLiteral("hover")) ? 0 : 1;
	}
	{
		FilterInsertSeam seam;
		seam.resize(960, 10);
		seam.show();
		QEnterEvent seamEnter(QPointF(20, 5), QPointF(20, 5), QPointF(20, 5));
		QApplication::sendEvent(&seam, &seamEnter);
		QApplication::processEvents();
		failures += saveGrab(&seam, outDir, skinId, mode, QStringLiteral("seam"), QStringLiteral("hover")) ? 0 : 1;
	}

	// The auto-update toast over a plain palette host, with the real message
	// template so per-skin QSS is judged against representative text.
	{
		QWidget host;
		host.resize(960, 90);
		host.setAutoFillBackground(true);
		UpdateToast* toast = new UpdateToast(&host);
		host.show();
		toast->showMessage(QStringLiteral("Update 2.99.0 has been downloaded and will be applied when you close the editor."), 0);
		QApplication::processEvents();
		failures += saveGrab(toast, outDir, skinId, mode, QStringLiteral("toast"), QStringLiteral("normal")) ? 0 : 1;
	}

	// Every stateful form control the skins restyle, in every state that has
	// its own QSS rule: check/partial/disabled checkboxes and radio buttons.
	// This is the shot that catches a stylesheet whose checked box is only a
	// filled square (the engine replaces native indicator drawing, so the
	// check glyph must come from the sheet itself).
	{
		QWidget controls;
		controls.setAutoFillBackground(true);
		QHBoxLayout* controlsLayout = new QHBoxLayout(&controls);
		controlsLayout->setContentsMargins(16, 12, 16, 12);
		controlsLayout->setSpacing(18);

		QCheckBox* checkedBox = new QCheckBox(QStringLiteral("Checked"), &controls);
		checkedBox->setChecked(true);
		controlsLayout->addWidget(checkedBox);

		QCheckBox* uncheckedBox = new QCheckBox(QStringLiteral("Unchecked"), &controls);
		controlsLayout->addWidget(uncheckedBox);

		QCheckBox* partialBox = new QCheckBox(QStringLiteral("Partial"), &controls);
		partialBox->setTristate(true);
		partialBox->setCheckState(Qt::PartiallyChecked);
		controlsLayout->addWidget(partialBox);

		QCheckBox* disabledBox = new QCheckBox(QStringLiteral("Disabled"), &controls);
		disabledBox->setChecked(true);
		disabledBox->setEnabled(false);
		controlsLayout->addWidget(disabledBox);

		QRadioButton* radioOn = new QRadioButton(QStringLiteral("Radio on"), &controls);
		radioOn->setChecked(true);
		controlsLayout->addWidget(radioOn);

		QRadioButton* radioOff = new QRadioButton(QStringLiteral("Radio off"), &controls);
		// Two radios in one widget share a button group; keep the second one
		// out of it so the first stays checked.
		radioOff->setAutoExclusive(false);
		controlsLayout->addWidget(radioOff);

		controlsLayout->addStretch(1);
		controls.resize(760, controls.sizeHint().height());
		controls.show();
		QApplication::processEvents();
		failures += saveGrab(&controls, outDir, skinId, mode, QStringLiteral("controls"), QStringLiteral("normal")) ? 0 : 1;
	}

	// The skinned file dialog (GUIHelper::prepareFileDialog): Qt's widget
	// dialog under the app-wide sheet, its navigation buttons dressed through
	// ISkin::styleFileDialog. The fixture's pinned timestamps keep the Detail
	// columns deterministic, and the sidebar is overridden with fixture-local
	// folders so its labels do not depend on this machine's user folders.
	{
		const QString fixturePath = buildFileDialogFixture(outDir);
		if (fixturePath.isEmpty())
		{
			qWarning("SkinGallery: could not build the file dialog fixture");
			failures += 1;
		}
		else
		{
			QFileDialog dialog(nullptr, QStringLiteral("Open file"), fixturePath, QStringLiteral("*.txt"));
			dialog.setFileMode(QFileDialog::ExistingFiles);
			dialog.setNameFilter(QStringLiteral("E-APO configurations (*.txt)"));
			GUIHelper::prepareFileDialog(dialog);
			dialog.setSidebarUrls({ QUrl::fromLocalFile(QDir(fixturePath).filePath(QStringLiteral("config"))),
				QUrl::fromLocalFile(QDir(fixturePath).filePath(QStringLiteral("IRs"))) });
			dialog.show();
			// QFileSystemModel lists directories on a worker thread; wait for
			// the four fixture rows (config, IRs, demo, voice) to land before
			// grabbing.
			QTreeView* view = dialog.findChild<QTreeView*>(QStringLiteral("treeView"));
			QElapsedTimer listTimer;
			listTimer.start();
			while (listTimer.elapsed() < 3000
				&& (view == nullptr || view->model() == nullptr || view->model()->rowCount(view->rootIndex()) < 4))
				QApplication::processEvents(QEventLoop::AllEvents, 50);
			failures += saveGrab(&dialog, outDir, skinId, mode, QStringLiteral("filedialog"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// The analysis dock's response graph over the deterministic synthetic
	// spectrum (boosts, cuts and a clipping shelf so the over-0dB emphasis
	// shows), at rest and with the pinned cursor readout.
	{
		EqGraphView graph;
		graph.resize(940, 220);
		graph.setResponse(galleryAnalysisResponse(), QStringLiteral("All"));
		graph.show();
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("normal")) ? 0 : 1;
		graph.setPreviewCursor(0.62);
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("cursor")) ? 0 : 1;
	}

	// The same graph showing what a magnitude plot cannot: an all-pass's phase
	// and its group delay. The fixture is a real 2nd-order all-pass evaluated
	// from the engine's own coefficients, so these shots show the filter rather
	// than a drawing of one, and the cursor is pinned to prove the readout
	// changes unit with the metric.
	{
		EqGraphView graph;
		graph.resize(940, 220);
		graph.setResponse(galleryAllPassResponse(), QStringLiteral("All"));
		graph.setPreviewCursor(0.62);
		graph.show();
		graph.setMetric(AnalysisMetric::PhaseDegrees);
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("phase")) ? 0 : 1;
		graph.setMetric(AnalysisMetric::GroupDelayMs);
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("groupdelay")) ? 0 : 1;
	}

	// The metric switch itself, in its three positions plus hover, so the new
	// segmented control is judged as a control and not only in situ.
	{
		SegmentedControl segment;
		segment.setLabels({ QStringLiteral("Mag"), QStringLiteral("Phase"), QStringLiteral("GD") });
		segment.resize(230, segment.sizeHint().height());
		segment.show();
		// setPreviewState rather than setCurrentIndex: the indicator animates,
		// and a shot taken while it travels differs from run to run.
		segment.setPreviewState(0, -1);
		QApplication::processEvents();
		failures += saveGrab(&segment, outDir, skinId, mode, QStringLiteral("segment"), QStringLiteral("normal")) ? 0 : 1;
		segment.setPreviewState(1, -1);
		QApplication::processEvents();
		failures += saveGrab(&segment, outDir, skinId, mode, QStringLiteral("segment"), QStringLiteral("selected")) ? 0 : 1;
		segment.setPreviewState(1, 2);
		QApplication::processEvents();
		failures += saveGrab(&segment, outDir, skinId, mode, QStringLiteral("segment"), QStringLiteral("hover")) ? 0 : 1;
	}

	// The Copy channel fold over a synthetic 7.1 endpoint. The row matrix's
	// card path is deliberately deviceless, so the device-channel seeding
	// (and therefore the fold: collapsed rows, the reveal control, the
	// add-channel entry) never shows there. Four states: the routed line
	// collapsed to its two involved channels, the same line fully expanded,
	// the add-channel editor open with a name typed, and an empty Copy
	// showing its two representative channels.
	{
		auto surround = std::make_shared<GalleryAPOInfo>(
			L"Speakers", L"Example Audio 7.1", false, true, 8, 0x63F);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy: VC=0.5*L+0.5*R R=L"), QStringLiteral("Copy:") },
			surround, 0x63F);
		if (rows.size() != 2)
		{
			qWarning("SkinGallery: copy fold scene expected 2 rows, got %lld (%s %s)",
				static_cast<long long>(rows.size()), qPrintable(skinId), qPrintable(mode));
			failures += 4;
		}
		else
		{
			FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
			auto settle = [&]() {
				QApplication::processEvents();
				if (table != nullptr && table->layout() != nullptr)
					table->layout()->activate();
				QApplication::processEvents();
			};
			failures += assertNoHorizontalScrollBar(rows[0], skinId, mode, QStringLiteral("copyfold"), QStringLiteral("normal"));
			failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("normal")) ? 0 : 1;
			failures += saveGrab(rows[1], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("empty")) ? 0 : 1;
			RoutingView* view = rows[0]->findChild<RoutingView*>();
			if (view == nullptr)
			{
				qWarning("SkinGallery: copy fold scene has no routing view (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures += 2;
			}
			else
			{
				view->galleryShowcase(QStringLiteral("expanded"));
				settle();
				failures += assertNoHorizontalScrollBar(rows[0], skinId, mode, QStringLiteral("copyfold"), QStringLiteral("expanded"));
				failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("expanded")) ? 0 : 1;
				view->galleryShowcase(QStringLiteral("addChannel"));
				settle();
				failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("editor")) ? 0 : 1;
				// The inline source editor on the routed line's first summand,
				// next to the open prompt. Only the minimal step list has one;
				// the other renderers edit in place and record the same view
				// again here, which keeps the shot-count contract uniform.
				view->galleryShowcase(QStringLiteral("editSource:VC"));
				settle();
				failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("sourceeditor")) ? 0 : 1;
			}
		}
	}

	// MultiConvolution shares Copy's target-channel fold, but its source side
	// is the selected WAV's fixed channel list. This 4-channel BRIR over a 7.1
	// endpoint proves both halves of that contract in every renderer: the
	// collapsed card lists only mapped L/R outputs while retaining source
	// ports 0..3, and the reveal control expands all eight outputs.
	{
		auto surround = std::make_shared<GalleryAPOInfo>(
			L"Speakers", L"Example Audio 7.1", false, true, 8, 0x63F);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("MultiConvolution: L=0+1 R=2+3 brir.wav") },
			surround, 0x63F);
		if (rows.size() != 1)
		{
			qWarning("SkinGallery: MultiConvolution fold scene expected 1 row, got %lld (%s %s)",
				static_cast<long long>(rows.size()), qPrintable(skinId), qPrintable(mode));
			failures += 2;
		}
		else
		{
			FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
			auto settle = [&]() {
				QApplication::processEvents();
				if (table != nullptr && table->layout() != nullptr)
					table->layout()->activate();
				QApplication::processEvents();
			};
			failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
				QStringLiteral("multiconvfold"), QStringLiteral("normal"));
			failures += saveGrab(rows[0], outDir, skinId, mode,
				QStringLiteral("multiconvfold"), QStringLiteral("normal")) ? 0 : 1;
			// MultiConvolution rebuilds its routing view after file metadata
			// and device channels arrive. The superseded view is hidden and
			// deleteLater'd, but processEvents does not guarantee deferred
			// deletion here; choose the live visible child explicitly.
			RoutingView* view = nullptr;
			for (RoutingView* candidate : rows[0]->findChildren<RoutingView*>())
				if (candidate->isVisible())
					view = candidate;
			if (view == nullptr)
			{
				qWarning("SkinGallery: MultiConvolution fold scene has no routing view (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures++;
			}
			else
			{
				view->galleryShowcase(QStringLiteral("expanded"));
				settle();
				failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
					QStringLiteral("multiconvfold"), QStringLiteral("expanded"));
				failures += saveGrab(rows[0], outDir, skinId, mode,
					QStringLiteral("multiconvfold"), QStringLiteral("expanded")) ? 0 : 1;
			}
		}
	}

	// The dynamic-commands logic block (If/ElseIf/Else/EndIf/Eval), captured
	// as one whole-table shot so the scope presentation that spans rows
	// (rails, brackets, the relay bus) is judged in context. The offscreen
	// gallery runs no analysis, so the engine load facts every skin's
	// presentation reads (branch lamps, TRUE/FALSE readouts, cancelled rows)
	// are injected synthetically: the outer If is taken, the nested If is
	// false, the ElseIf chain is short-circuited and the Else is dead.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, {
			QStringLiteral("Eval: bassBoost = 6"),
			QStringLiteral("If: outputChannelCount >= 6"),
			QStringLiteral("Preamp: -3 dB"),
			QStringLiteral("If: sampleRate > 48000"),
			QStringLiteral("Delay: 0.25 ms"),
			QStringLiteral("EndIf:"),
			QStringLiteral("ElseIf: outputChannelCount == 4"),
			QStringLiteral("Preamp: -1.5 dB"),
			QStringLiteral("Else:"),
			QStringLiteral("Preamp: 0 dB"),
			QStringLiteral("EndIf:"),
			QStringLiteral("Delay: 5 ms")
		});
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("SkinGallery: logic scene has no table (%s %s)", qPrintable(skinId), qPrintable(mode));
			failures += 1;
		}
		else
		{
			auto fact = [](int line, ConfigLoadTraceEntry::Kind kind, ConfigLoadTraceEntry::Result result,
				bool active, const wchar_t* text = L"") {
				ConfigLoadTraceEntry entry;
				entry.line = line;
				entry.kind = kind;
				entry.result = result;
				entry.active = active;
				entry.text = text;
				return entry;
			};
			table->setLoadTraceFacts({
				fact(1, ConfigLoadTraceEntry::Kind::Eval, ConfigLoadTraceEntry::Result::NotEvaluated, false, L"6"),
				fact(2, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::True, true),
				fact(4, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::False, false),
				fact(5, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(7, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(8, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(9, ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(10, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false)
			});
			QApplication::processEvents();
			failures += saveGrab(table, outDir, skinId, mode, QStringLiteral("logic"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// A Channel: selection group, captured as one whole-table shot: member
	// rows inherit the selection's badges (channel identity on every member,
	// where the rail only shows extent), the Copy member keeps its own
	// destination badges, and Channel: ALL returns the tail to unbadged.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 560);
		buildRows(scrollArea, configPath, {
			QStringLiteral("Channel: L R"),
			QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71"),
			QStringLiteral("Delay: 5 ms"),
			QStringLiteral("Copy: SL=L SR=R"),
			QStringLiteral("Channel: ALL"),
			QStringLiteral("Preamp: -3 dB")
		});
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("SkinGallery: channel scope scene has no table (%s %s)", qPrintable(skinId), qPrintable(mode));
			failures += 1;
		}
		else
		{
			QApplication::processEvents();
			failures += saveGrab(table, outDir, skinId, mode, QStringLiteral("channelscope"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// The expanded state is part of the card contract: all expert controls
	// remain reachable without changing the compact default presentation.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		const QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, {
			QStringLiteral("Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136")
		});
		if (rows.size() != 1)
		{
			qWarning("SkinGallery: Velvet advanced scene has no row (%s %s)",
				qPrintable(skinId), qPrintable(mode));
			failures++;
		}
		else
		{
			QToolButton* toggle = rows[0]->findChild<QToolButton*>(QStringLiteral("VelvetAdvancedToggle"));
			if (toggle == nullptr)
			{
				qWarning("SkinGallery: Velvet advanced toggle missing (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures++;
			}
			else
			{
				toggle->setChecked(true);
				QApplication::processEvents();
				if (rows[0]->layout() != nullptr)
					rows[0]->layout()->activate();
				QApplication::processEvents();
				failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
					QStringLiteral("velvet-advanced"), QStringLiteral("normal"));
				failures += saveGrab(rows[0], outDir, skinId, mode,
					QStringLiteral("velvet-advanced"), QStringLiteral("normal")) ? 0 : 1;
			}
		}
	}

	// A compact dock width catches accidental horizontal minimums in the
	// parameter row. The primary controls must wrap/contract without forcing a
	// horizontal scrollbar; advanced controls stay folded.
	{
		QScrollArea scrollArea;
		scrollArea.resize(520, 640);
		const QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, {
			QStringLiteral("Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136")
		});
		if (rows.size() != 1)
		{
			qWarning("SkinGallery: Velvet narrow scene has no row (%s %s)",
				qPrintable(skinId), qPrintable(mode));
			failures++;
		}
		else
		{
			failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
				QStringLiteral("velvet-narrow"), QStringLiteral("normal"));
			failures += saveGrab(rows[0], outDir, skinId, mode,
				QStringLiteral("velvet-narrow"), QStringLiteral("normal")) ? 0 : 1;
		}
	}
	return failures;
}
}

namespace SkinGallery
{
// Heritage (legacy rows) verification: the mode is a single unskinned
// presentation, so instead of the per-skin per-row matrix it renders two
// whole-table dumps (active and commented rows) for eyeball regression checks.
// Triggered by EAPO_GALLERY_LEGACY=1, used by the local runner scripts.
int renderHeritage(const QDir& outDir, const QString& configPath)
{
	SkinManager::instance()->applyHeritage();

	int failures = 0;
	for (int commented = 0; commented <= 1; commented++)
	{
		QList<QString> lines;
		for (const GalleryRow& row : galleryRows())
			lines.append(commented ? QStringLiteral("# ") + row.line : row.line);

		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, lines);
		scrollArea.show();
		QCoreApplication::processEvents();

		QPixmap dump = scrollArea.widget()->grab();
		const QString fileName = outDir.filePath(QStringLiteral("heritage_%1.png")
				.arg(commented ? QStringLiteral("disabled") : QStringLiteral("normal")));
		if (dump.isNull() || !dump.save(fileName))
		{
			qWarning("SkinGallery: could not write %s", qPrintable(fileName));
			failures++;
		}
	}
	return failures;
}

int runSwitchTest(const QStringList& arguments)
{
	Q_UNUSED(arguments);

	qWarning("SkinSwitchTest: starting");

	FilterInsertSeam accessibilitySeam;
	int seamActivations = 0;
	QObject::connect(&accessibilitySeam, &FilterInsertSeam::activated,
		[&seamActivations]() { seamActivations++; });
	QKeyEvent activateSeam(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
	QApplication::sendEvent(&accessibilitySeam, &activateSeam);
	if (accessibilitySeam.focusPolicy() != Qt::StrongFocus || seamActivations != 1
		|| accessibilitySeam.accessibleName().isEmpty())
	{
		qWarning("SkinSwitchTest: insertion seam lacks keyboard accessibility parity");
		return 1;
	}

	QWidget toastHost;
	UpdateToast timerProbe(&toastHost);
	timerProbe.showMessage(QStringLiteral("temporary"), 15000);
	const QTimer* const autoHideTimer = timerProbe.findChild<QTimer*>(QStringLiteral("UpdateToastAutoHide"));
	if (autoHideTimer == nullptr || !autoHideTimer->isActive())
	{
		qWarning("SkinSwitchTest: update toast does not own a restartable auto-hide timer");
		return 1;
	}
	timerProbe.showMessage(QStringLiteral("persistent"), 0);
	if (autoHideTimer->isActive())
	{
		qWarning("SkinSwitchTest: persistent update toast kept a stale auto-hide timer");
		return 1;
	}

	// Scratch reference targets so the reference cards resolve like the
	// gallery's; EAPO_SKIN_GALLERY also skips the audio-service ACL probe.
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("SkinSwitchTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("SkinSwitchTest: cannot write reference target files");
		return 2;
	}

	// A config heavy enough for the historical failure mode: the field crash
	// and the seconds-per-switch regression both needed a loaded document,
	// not the empty tree the gallery switches under. Six copies of the
	// representative rows exercise every card type at over a hundred rows.
	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	buildRows(scrollArea, configPath, lines);
	FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
	if (table == nullptr)
	{
		qWarning("SkinSwitchTest: table construction failed");
		return 1;
	}
	// Gallery tables deliberately have no MainWindow. This supported host mode
	// must keep navigation callbacks harmless.
	table->openConfig(QString());

	// A live TitleBar rides along: its caption glyphs are tinted icons, not
	// QSS, so a switch path that forgets to re-dress them leaves them in the
	// previous skin's ink (the field report: black glyphs on the dark strip
	// after a light->dark toggle). Dark/light alternate every switch below,
	// so a stale icon is guaranteed to mismatch the active ink.
	QWidget titleHost;
	TitleBar titleBar(&titleHost);
	QToolButton* captionButton = titleBar.findChild<QToolButton*>(QStringLiteral("TitleBarMin"));
	if (captionButton == nullptr)
	{
		qWarning("SkinSwitchTest: TitleBarMin caption button not found");
		return 1;
	}

	// The main toolbar rides along in a real QMainWindow top area, wired the
	// way MainWindow wires it (skinChanged -> styleMainToolbar). The gallery
	// builds a fresh replica per skin, so per-skin chrome state that survives
	// on ONE long-lived toolbar across revisits (rack's plate + ear-spacer
	// actions, matrix's board layers) is only exercised here - the field
	// report was the whole action train vanishing after returning to an
	// already-visited skin.
	QMainWindow probeWindow;
	// Wide enough that the full action train genuinely fits in every skin's
	// paddings: a hidden item then always means the layout lost the room, not
	// that the room was honestly missing.
	probeWindow.resize(1600, 768);
	QToolBar* probeToolBar = buildToolbarReplica(nullptr);
	probeWindow.addToolBar(Qt::TopToolBarArea, probeToolBar);
	probeWindow.setCentralWidget(new QWidget(&probeWindow));
	probeWindow.show();
	QObject::connect(SkinManager::instance(), &SkinManager::skinChanged, probeToolBar,
		[probeToolBar](const SkinTokens&) { SkinManager::instance()->styleMainToolbar(probeToolBar); });

	// Everything in the action train must stay alive and laid out: an action
	// reported invisible, an item widget QToolBarLayout hid (overflow into the
	// extension popup counts - the real toolbar never overflows at 1024px), or
	// a collapsed toolbar all reproduce the "toolbar is gone" field state.
	const auto checkToolbar = [&probeWindow, probeToolBar](const QString& switchName) {
		int problems = 0;
		QLabel* formatBadge = probeToolBar->findChild<QLabel*>(
			QStringLiteral("DeviceFormatBadge"), Qt::FindDirectChildrenOnly);
		if (formatBadge == nullptr
			|| formatBadge->property("severity").toString() != QLatin1String("warning"))
		{
			qWarning("SkinSwitchTest: %s: representative DeviceFormatBadge is missing",
				qPrintable(switchName));
			problems++;
		}
		if (!probeToolBar->isVisibleTo(&probeWindow))
		{
			qWarning("SkinSwitchTest: %s: main toolbar widget is hidden", qPrintable(switchName));
			problems++;
		}
		if (probeToolBar->height() < 16)
		{
			qWarning("SkinSwitchTest: %s: main toolbar collapsed to %dpx",
				qPrintable(switchName), probeToolBar->height());
			problems++;
		}
		for (QAction* action : probeToolBar->actions())
		{
			QWidget* item = probeToolBar->widgetForAction(action);
			// State-driven items legitimately hide; every other item belongs
			// to the structural health contract.
			if (item != nullptr
				&& MainToolbarKit::visibilityIsDataObjectNames().contains(item->objectName()))
				continue;
			const QString label = item != nullptr && !item->objectName().isEmpty()
				? item->objectName() : action->objectName();
			if (!action->isVisible())
			{
				qWarning("SkinSwitchTest: %s: toolbar action %s turned invisible",
					qPrintable(switchName), qPrintable(label));
				problems++;
			}
			else if (item != nullptr && item->isHidden())
			{
				qWarning("SkinSwitchTest: %s: toolbar item %s was hidden by the layout (item hint %dpx, bar %dpx wide, bar hint %dpx)",
					qPrintable(switchName), qPrintable(label),
					item->sizeHint().width(), probeToolBar->width(),
					probeToolBar->sizeHint().width());
				problems++;
			}
		}
		// Pixels, not flags: the field bug rendered the strip blank while
		// every logical probe stayed healthy.
		if (probeToolBar->isVisible()
			&& ToolbarPixelProbe::renderIsBlank(probeToolBar->grab().toImage().convertToFormat(QImage::Format_RGB32)))
		{
			qWarning("SkinSwitchTest: %s: toolbar rendered blank (controls not painted)",
				qPrintable(switchName));
			problems++;
		}
		return problems;
	};

	// Generous ceiling: a healthy switch is well under a second offscreen,
	// the regression class this guards against cost multiple seconds per
	// switch, and CI runners are slow and variable. Overridable for local
	// tuning.
	bool limitOk = false;
	int limitMs = qEnvironmentVariableIntValue("EAPO_SWITCH_LIMIT_MS", &limitOk);
	if (!limitOk || limitMs <= 0)
		limitMs = 8000;
	bool warningOk = false;
	int warningMs = qEnvironmentVariableIntValue("EAPO_SWITCH_WARN_MS", &warningOk);
	if (!warningOk || warningMs <= 0 || warningMs >= limitMs)
		warningMs = 0;

	int failures = 0;
	const auto checkPaintOnlyChrome = [&scrollArea, &failures](const QString& objectName) {
		const QList<QWidget*> widgets = scrollArea.findChildren<QWidget*>(objectName);
		if (widgets.isEmpty())
		{
			qWarning("SkinSwitchTest: paint-only chrome %s is missing", qPrintable(objectName));
			failures++;
			return;
		}
		for (QWidget* widget : widgets)
		{
			if (!widget->testAttribute(Qt::WA_NoSystemBackground))
			{
				qWarning("SkinSwitchTest: paint-only chrome %s accepts a framework background",
					qPrintable(objectName));
				failures++;
				break;
			}
		}
	};
	QApplication::processEvents();
	failures += checkToolbar(QStringLiteral("baseline (before any switch)"));
	{
		// LegacyRows still uses CopyFilterGUI. Its QGraphicsView does not own
		// the scene, so the GUI must parent it explicitly; allWidgets() cannot
		// detect this leak because QGraphicsScene is not a QWidget.
		CopyFilterGUI* legacyCopy = new CopyFilterGUI({}, table);
		QGraphicsView* graphicsView = legacyCopy->findChild<QGraphicsView*>();
		QPointer<QGraphicsScene> scene = graphicsView != nullptr ? graphicsView->scene() : nullptr;
		if (scene.isNull())
		{
			qWarning("SkinSwitchTest: legacy CopyFilterGUI scene was not created");
			failures++;
		}
		delete legacyCopy;
		if (!scene.isNull())
		{
			qWarning("SkinSwitchTest: deleting CopyFilterGUI did not delete its scene");
			delete scene.data();
			failures++;
		}
	}
	qint64 worstMs = 0;
	QString worstName;
	const int rounds = 3;
	for (int round = 1; round <= rounds; round++)
	{
		for (ISkin* skin : Skins::all())
		{
			for (int darkIndex = 0; darkIndex < 2; darkIndex++)
			{
				const bool dark = darkIndex == 0;
				const QString name = QStringLiteral("%1/%2").arg(skin->id(), dark ? QStringLiteral("dark") : QStringLiteral("light"));

				QElapsedTimer timer;
				timer.start();
				// MainWindow::skinSelected's exact live sequence: tear the
				// rows down BEFORE the global stylesheet swap (which also
				// re-derives the palette), rebuild after.
				table->clearRows();
				const qint64 clearMs = timer.restart();
				SkinManager::instance()->applySkin(skin->id(), dark);
				const qint64 applyMs = timer.restart();
				table->updateGuis();
				QApplication::processEvents();
				// The live editor returns to the event loop between switches,
				// which is when deleteLater victims (combo popup containers,
				// editor internals) actually die; a bare processEvents() does
				// not deliver DeferredDelete, and without this the harness
				// accumulates a dead generation per switch that the real app
				// never keeps.
				QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
				QApplication::processEvents();
				const qint64 rebuildMs = timer.elapsed();
				const qint64 elapsed = clearMs + applyMs + rebuildMs;

				if (SkinManager::instance()->currentSkinId() != skin->id())
				{
					qWarning("SkinSwitchTest: switch to %s resolved to %s",
						qPrintable(name), qPrintable(SkinManager::instance()->currentSkinId()));
					failures++;
				}
				if (skin->id() == QLatin1String("soft"))
					checkPaintOnlyChrome(QStringLiteral("SoftReferenceTile"));
				{
					// Caption ink check. tintedIcon paints every covered pixel
					// in the ink colour, so the strongest-coverage pixel must
					// sit on the active skin's text colour. Tolerance, not
					// equality: the thin minimize stroke has no fully opaque
					// pixel at 14 px and unpremultiply rounding shifts channels
					// by a few counts - a stale light/dark ink is off by ~100+.
					const QColor ink(SkinManager::instance()->tokens().text);
					const QImage glyph = captionButton->icon()
						.pixmap(QSize(14, 14)).toImage().convertToFormat(QImage::Format_ARGB32);
					int bestAlpha = 0;
					QColor bestPixel;
					for (int y = 0; y < glyph.height(); y++)
						for (int x = 0; x < glyph.width(); x++)
						{
							const QColor pixel = glyph.pixelColor(x, y);
							if (pixel.alpha() > bestAlpha)
							{
								bestAlpha = pixel.alpha();
								bestPixel = pixel;
							}
						}
					const bool inkMatched = bestAlpha > 0
						&& qAbs(bestPixel.red() - ink.red()) <= 8
						&& qAbs(bestPixel.green() - ink.green()) <= 8
						&& qAbs(bestPixel.blue() - ink.blue()) <= 8;
					if (!inkMatched)
					{
						qWarning("SkinSwitchTest: caption icon did not follow the switch to %s (ink %s, glyph %s a%d)",
							qPrintable(name), qPrintable(ink.name()), qPrintable(bestPixel.name()), bestAlpha);
						failures++;
					}
				}
				failures += checkToolbar(name);
				if (round == 1 && darkIndex == 0)
				{
					// The skinned file dialog's icon provider must serve the
					// drive glyph for both drive spellings: the real root
					// ("C:/") and the slash-less shell name ("C:") that
					// QFileSystemModel stores drive nodes under and rebuilds
					// node icons from on setIconProvider. The bare spelling
					// is neither isRoot() nor a file, which dressed every
					// sidebar drive with the folder pictogram in the field.
					QFileDialog probeDialog;
					probeDialog.setOption(QFileDialog::DontUseNativeDialog);
					SkinManager::instance()->styleFileDialog(&probeDialog);
					if (SkinFileIconProvider* provider
						= dynamic_cast<SkinFileIconProvider*>(probeDialog.iconProvider()))
					{
						const qint64 driveKey
							= provider->icon(QAbstractFileIconProvider::Drive).cacheKey();
						const qint64 folderKey
							= provider->icon(QAbstractFileIconProvider::Folder).cacheKey();
						if (driveKey == folderKey
							|| provider->icon(QFileInfo(QStringLiteral("C:"))).cacheKey() != driveKey
							|| provider->icon(QFileInfo(QStringLiteral("C:/"))).cacheKey() != driveKey)
						{
							qWarning("SkinSwitchTest: %s file-dialog provider does not classify a drive root as the drive glyph",
								qPrintable(skin->id()));
							failures++;
						}
					}
				}
				if (elapsed > limitMs)
				{
					qWarning("SkinSwitchTest: switch to %s took %lld ms (limit %d ms)",
						qPrintable(name), static_cast<long long>(elapsed), limitMs);
					failures++;
				}
				else if (warningMs > 0 && elapsed > warningMs)
				{
					qWarning("SkinSwitchTest: switch to %s took %lld ms (warning %d ms; hard limit %d ms)",
						qPrintable(name), static_cast<long long>(elapsed), warningMs, limitMs);
				}
				if (elapsed > worstMs)
				{
					worstMs = elapsed;
					worstName = name;
				}
				qWarning("SkinSwitchTest: round %d %s: %lld ms (clear %lld, apply %lld, rebuild %lld, widgets %lld)",
					round, qPrintable(name), static_cast<long long>(elapsed),
					static_cast<long long>(clearMs), static_cast<long long>(applyMs), static_cast<long long>(rebuildMs),
					static_cast<long long>(QApplication::allWidgets().size()));
			}
		}
	}

	{
		// Diagnostic: class histogram of the surviving widgets, top entries.
		QHash<QByteArray, int> histogram;
		for (QWidget* widget : QApplication::allWidgets())
			histogram[widget->metaObject()->className()]++;
		const int legacyCopyWidgets = histogram.value(QByteArrayLiteral("CopyFilterGUI"));
		if (legacyCopyWidgets != 0)
		{
			qWarning("SkinSwitchTest: modern cards retained %d CopyFilterGUI widgets", legacyCopyWidgets);
			failures++;
		}
		QList<QPair<int, QByteArray>> ranked;
		for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it)
			ranked.append({ it.value(), it.key() });
		std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (int i = 0; i < qMin(10, int(ranked.size())); i++)
			qWarning("SkinSwitchTest: widget census %s x%d", ranked[i].second.constData(), ranked[i].first);

		// Top-level population: a growing count here is how the leaked
		// parentless CopyFilterGUI generation was originally spotted.
		int windows = 0;
		for (QWidget* widget : QApplication::allWidgets())
			if (widget->isWindow())
				windows++;
		qWarning("SkinSwitchTest: %d top-level widgets", windows);
	}

	qWarning("SkinSwitchTest: %d switches over %d rows, worst %lld ms (%s), warning %d ms, limit %d ms, failures %d",
		rounds * int(Skins::all().size()) * 2, int(lines.size()), static_cast<long long>(worstMs),
		qPrintable(worstName), warningMs, limitMs, failures);

	// Same no-teardown exit as run(): everything is flushed, and unwinding
	// the offscreen QApplication can hang the process on a leftover resource.
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

int runCardMoveTest(const QStringList& arguments)
{
	Q_UNUSED(arguments);

	qWarning("CardMoveTest: starting");

	// Scratch reference targets so the reference cards resolve like the
	// gallery's; EAPO_SKIN_GALLERY also skips the audio-service ACL probe.
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("CardMoveTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("CardMoveTest: cannot write reference target files");
		return 2;
	}

	// The same 100+ row document as the switch test: the field lag needs a
	// loaded document, and six copies of the representative rows exercise
	// every card type.
	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	buildRows(scrollArea, configPath, lines);
	FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
	if (table == nullptr)
	{
		qWarning("CardMoveTest: table construction failed");
		return 1;
	}
	// Gallery tables deliberately have no MainWindow; navigation callbacks
	// must stay harmless in this supported host mode.
	table->openConfig(QString());

	// Budget contract like the switch test: the limit fails the gate, the
	// warning only logs. The field regression class this measures cost 5-6 s
	// per move on a fast desktop, so even the generous default limit would
	// catch a return to a full rebuild on a slow runner.
	bool limitOk = false;
	int limitMs = qEnvironmentVariableIntValue("EAPO_MOVE_LIMIT_MS", &limitOk);
	if (!limitOk || limitMs <= 0)
		limitMs = 20000;
	bool warningOk = false;
	int warningMs = qEnvironmentVariableIntValue("EAPO_MOVE_WARN_MS", &warningOk);
	if (!warningOk || warningMs <= 0 || warningMs >= limitMs)
		warningMs = 0;

	int failures = 0;
	int moves = 0;
	qint64 worstMs = 0;
	QString worstName;
	for (ISkin* skin : Skins::all())
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
			const QString name = QStringLiteral("%1/%2").arg(skin->id(),
				dark ? QStringLiteral("dark") : QStringLiteral("light"));

			// Fresh rows under this skin, in the live switch order (tear down
			// before the stylesheet swap, rebuild after).
			table->clearRows();
			SkinManager::instance()->applySkin(skin->id(), dark);
			table->updateGuis();
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();

			const int rowCount = int(table->documentItems().count());
			if (rowCount != lines.size())
			{
				qWarning("CardMoveTest: %s expected %lld rows, found %d",
					qPrintable(name), static_cast<long long>(lines.size()), rowCount);
				failures++;
				continue;
			}

			// One card moved down one row, then back up: two timed commits of
			// the drag-move path per scene, and the document leaves the scene
			// in its original order.
			const int from = rowCount / 2;
			for (int pass = 0; pass < 2; pass++)
			{
				const QList<QString> before = table->getLines();
				const int sourceRow = pass == 0 ? from : from + 1;
				const int targetRow = pass == 0 ? from + 1 : from;
				const int dropRow = pass == 0 ? from + 2 : from;
				FilterTable::Item* moved = table->documentItems().at(sourceRow);

				QElapsedTimer timer;
				timer.start();
				table->moveRows({ moved }, dropRow);
				QApplication::processEvents();
				const qint64 elapsed = timer.elapsed();
				moves++;

				QList<QString> expected = before;
				expected.move(sourceRow, targetRow);
				if (table->getLines() != expected)
				{
					qWarning("CardMoveTest: %s move %d produced a wrong document order",
						qPrintable(name), pass + 1);
					failures++;
				}
				// The moved line must stay the (only) selection, at its new
				// row - by position, not pointer, so both the copy-splice and
				// the item-preserving implementations of the move pass.
				const QSet<FilterTable::Item*>& selectedItems = table->getSelectedItems();
				if (selectedItems.size() != 1
					|| table->documentItems().indexOf(*selectedItems.cbegin()) != targetRow)
				{
					qWarning("CardMoveTest: %s move %d did not leave the moved row selected",
						qPrintable(name), pass + 1);
					failures++;
				}
				const int rowWidgets = int(table->findChildren<FilterCardRow*>(
					QString(), Qt::FindDirectChildrenOnly).count());
				if (rowWidgets != rowCount)
				{
					qWarning("CardMoveTest: %s move %d left %d row widgets for %d rows",
						qPrintable(name), pass + 1, rowWidgets, rowCount);
					failures++;
				}

				if (elapsed > limitMs)
				{
					qWarning("CardMoveTest: %s move %d took %lld ms (limit %d ms)",
						qPrintable(name), pass + 1, static_cast<long long>(elapsed), limitMs);
					failures++;
				}
				else if (warningMs > 0 && elapsed > warningMs)
				{
					qWarning("CardMoveTest: %s move %d took %lld ms (warning %d ms; hard limit %d ms)",
						qPrintable(name), pass + 1, static_cast<long long>(elapsed), warningMs, limitMs);
				}
				if (elapsed > worstMs)
				{
					worstMs = elapsed;
					worstName = name;
				}
				qWarning("CardMoveTest: %s move %d: %lld ms (rows %d, widgets %lld)",
					qPrintable(name), pass + 1, static_cast<long long>(elapsed), rowCount,
					static_cast<long long>(QApplication::allWidgets().size()));
			}
		}
	}

	qWarning("CardMoveTest: %d moves over %lld rows, worst %lld ms (%s), warning %d ms, limit %d ms, failures %d",
		moves, static_cast<long long>(lines.size()), static_cast<long long>(worstMs),
		qPrintable(worstName), warningMs, limitMs, failures);

	// Scroll-position contract for the row edits a user makes from a card's
	// own header (+ / - / text edit) and for the full rebuild behind every
	// other structural change. Field report: adding or removing one line
	// flashed the whole list and threw the view back to the top, so on a
	// long document every edit cost a scroll back down. The scrolled view
	// must stay where it was, within the height of the row that changed.
	{
		scrollArea.show();
		QApplication::processEvents();
		QScrollBar* bar = scrollArea.verticalScrollBar();
		const int rowCount = int(table->documentItems().count());
		const int middle = rowCount / 2;
		const int target = bar->maximum() / 2;
		const auto settle = [&bar]() {
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();
			return bar->value();
		};
		const auto check = [&](const char* what, int before, int after) {
			// One card's height is the most a splice above the viewport may
			// legitimately shift the content by.
			const int tolerance = 160;
			if (std::abs(after - before) > tolerance)
			{
				qWarning("CardMoveTest: scroll after %s jumped %d -> %d (max %d)", what, before, after, bar->maximum());
				failures++;
			}
			else
			{
				qWarning("CardMoveTest: scroll after %s held %d -> %d (max %d)", what, before, after, bar->maximum());
			}
		};

		bar->setValue(target);
		int before = settle();
		if (before <= 0)
		{
			qWarning("CardMoveTest: the document does not scroll (max %d); scroll contract not exercised", bar->maximum());
			failures++;
		}
		else
		{
			FilterTable::Item* anchor = table->documentItems().at(middle);
			FilterTable::Item* inserted = table->insertLine(QStringLiteral("Preamp: -1 dB"), anchor);
			check("header insert", before, settle());
			before = bar->value();
			table->removeLine(inserted);
			check("header remove", before, settle());
			before = bar->value();
			table->updateGuis();
			check("full rebuild", before, settle());
			// The live skin switch tears down first and rebuilds after the
			// stylesheet swap (MainWindow::skinSelected); the position must
			// survive that gap too.
			before = bar->value();
			table->clearRows();
			SkinManager::instance()->applySkin(Skins::all().first()->id(), false);
			table->updateGuis();
			check("skin switch", before, settle());
		}
	}

	// Same no-teardown exit as run().
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

int runCardSelectionTest(const QStringList& arguments)
{
	qWarning("CardSelectionTest: starting");
	QScreen* screen = QGuiApplication::primaryScreen();
	qWarning("CardSelectionTest: target platform=%s style=%s dpr=%.2f locale=%s",
		qPrintable(QGuiApplication::platformName()), qPrintable(qApp->style()->objectName()),
		screen != nullptr ? screen->devicePixelRatio() : 0.0, qPrintable(QLocale().name()));

	const int flagIndex = arguments.indexOf(QStringLiteral("--card-selection-test"));
	QDir outputDir;
	bool captureRequested = false;
	if (flagIndex >= 0 && flagIndex + 1 < arguments.size()
		&& !arguments.at(flagIndex + 1).startsWith(QStringLiteral("--")))
	{
		outputDir = QDir(arguments.at(flagIndex + 1));
		captureRequested = true;
		if (!outputDir.mkpath(QStringLiteral(".")))
		{
			qWarning("CardSelectionTest: cannot create output directory %s",
				qPrintable(outputDir.absolutePath()));
			return 2;
		}
	}

	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("CardSelectionTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("CardSelectionTest: cannot write reference target files");
		return 2;
	}

	const auto click = [](QWidget* target, const QPoint& localPos) {
		const QPoint globalPos = target->mapToGlobal(localPos);
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(localPos), QPointF(globalPos),
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(target, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(globalPos),
			Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(target, &release);
		QApplication::processEvents();
	};
	const auto pressKey = [](QWidget* target, int key) {
		QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
		QApplication::sendEvent(target, &press);
		QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
		QApplication::sendEvent(target, &release);
		QApplication::processEvents();
	};

	int failures = 0;
	int scenes = 0;
	for (ISkin* skin : Skins::all())
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
			const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
			const QString scene = QStringLiteral("%1/%2").arg(skin->id(), mode);
			SkinManager::instance()->applySkin(skin->id(), dark);

			QScrollArea scrollArea;
			scrollArea.resize(960, 720);
			QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, {
				QStringLiteral("Preamp: -1 dB"),
				QStringLiteral("Preamp: -2 dB"),
				QStringLiteral("Preamp: -3 dB")
			});
			FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
			if (table == nullptr || rows.size() != 3)
			{
				qWarning("CardSelectionTest: %s table construction failed", qPrintable(scene));
				failures++;
				continue;
			}

			const auto visualState = [&rows](const char* property, int expectedIndex) {
				int count = 0;
				bool expected = false;
				for (int i = 0; i < rows.size(); i++)
				{
					CommandRowFrame* frame = rows[i]->findChild<CommandRowFrame*>(
						QString(), Qt::FindDirectChildrenOnly);
					const bool active = frame != nullptr && frame->property(property).toBool();
					if (active)
						count++;
					if (i == expectedIndex)
						expected = active;
				}
				return qMakePair(count, expected);
			};

			// Drive the exact FilterTable pointer path, then compare the model
			// answer with the dynamic properties the skin actually renders.
			const QPoint secondHeader = rows[1]->mapTo(table, rows[1]->getHeaderRect().center());
			click(table, secondHeader);
			const bool headerModel = table->getSelectedItems().size() == 1
				&& table->getSelectedItems().contains(table->documentItems().at(1))
				&& table->getFocusedItem() == table->documentItems().at(1);
			const QPair<int, bool> headerSelected = visualState("selected", 1);
			const QPair<int, bool> headerFocused = visualState("focused", 1);
			const bool headerVisual = headerSelected.first == 1 && headerSelected.second
				&& headerFocused.first == 1 && headerFocused.second;
			if (!headerModel || !headerVisual)
			{
				qWarning("CardSelectionTest: %s header click model=%d visual=%d selectedFrames=%d focusedFrames=%d",
					qPrintable(scene), headerModel ? 1 : 0, headerVisual ? 1 : 0,
					headerSelected.first, headerFocused.first);
				failures++;
			}

			// Keyboard navigation is the closest regression path: it uses the
			// same state synchronizer but must retain the list's existing arrow
			// behavior after pointer clicks become card-aware.
			pressKey(table, Qt::Key_Up);
			const bool keyboardModel = table->getSelectedItems().size() == 1
				&& table->getSelectedItems().contains(table->documentItems().at(0))
				&& table->getFocusedItem() == table->documentItems().at(0);
			const QPair<int, bool> keyboardSelected = visualState("selected", 0);
			const QPair<int, bool> keyboardFocused = visualState("focused", 0);
			const bool keyboardVisual = keyboardSelected.first == 1 && keyboardSelected.second
				&& keyboardFocused.first == 1 && keyboardFocused.second;
			if (!keyboardModel || !keyboardVisual)
			{
				qWarning("CardSelectionTest: %s keyboard move model=%d visual=%d selectedFrames=%d focusedFrames=%d",
					qPrintable(scene), keyboardModel ? 1 : 0, keyboardVisual ? 1 : 0,
					keyboardSelected.first, keyboardFocused.first);
				failures++;
			}

			// QLineEdit consumes its own press. A plain click there must still
			// focus/select its owning card and collapse an old multi-selection.
			rows[2]->editText();
			QApplication::processEvents();
			table->selectAll();
			QApplication::processEvents();
			QLineEdit* rawEditor = rows[2]->findChild<QLineEdit*>(
				QStringLiteral("FilterCardRawEditor"));
			if (rawEditor == nullptr || !rawEditor->isVisible())
			{
				qWarning("CardSelectionTest: %s raw editor is unavailable", qPrintable(scene));
				failures++;
			}
			else
			{
				click(rawEditor, rawEditor->rect().center());
				const bool editorModel = table->getSelectedItems().size() == 1
					&& table->getSelectedItems().contains(table->documentItems().at(2))
					&& table->getFocusedItem() == table->documentItems().at(2);
				const QPair<int, bool> editorSelected = visualState("selected", 2);
				const QPair<int, bool> editorFocused = visualState("focused", 2);
				const bool editorVisual = editorSelected.first == 1 && editorSelected.second
					&& editorFocused.first == 1 && editorFocused.second;
				if (!editorModel || !editorVisual)
				{
					qWarning("CardSelectionTest: %s editor click model=%d visual=%d selectedFrames=%d focusedFrames=%d",
						qPrintable(scene), editorModel ? 1 : 0, editorVisual ? 1 : 0,
						editorSelected.first, editorFocused.first);
					failures++;
				}
			}

			if (captureRequested)
			{
				const QString shot = outputDir.filePath(QStringLiteral("%1_%2_card-selection.png")
					.arg(skin->id(), mode));
				if (!table->grab().save(shot, "PNG"))
				{
					qWarning("CardSelectionTest: %s could not write %s",
						qPrintable(scene), qPrintable(shot));
					failures++;
				}
			}

			qWarning("CardSelectionTest: %s complete", qPrintable(scene));
			scenes++;
		}
	}

	qWarning("CardSelectionTest: %d scenes, failures %d", scenes, failures);
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

int run(const QStringList& arguments)
{
	const int flagIndex = arguments.indexOf(QStringLiteral("--skin-gallery"));
	if (flagIndex < 0 || flagIndex + 1 >= arguments.size())
	{
		qWarning("Usage: Editor --skin-gallery <outDir> [--skin-gallery-skins id,id,...]");
		return 2;
	}

	QDir outDir(arguments.at(flagIndex + 1));
	if (!outDir.mkpath(QStringLiteral(".")))
	{
		qWarning("SkinGallery: cannot create output directory %s", qPrintable(outDir.absolutePath()));
		return 2;
	}

	QStringList skinIds;
	const int skinsIndex = arguments.indexOf(QStringLiteral("--skin-gallery-skins"));
	if (skinsIndex >= 0 && skinsIndex + 1 < arguments.size())
	{
		skinIds = arguments.at(skinsIndex + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
	}
	else
	{
		for (ISkin* skin : Skins::all())
			skinIds.append(skin->id());
	}

	// The gallery renders untranslated English by default (deterministic
	// output for the pixel gates). EAPO_GALLERY_LANG installs a shipped
	// catalog instead, for judging translated typography - the type-scale
	// round asked how the Korean strings read under the new sizes.
	const QByteArray galleryLang = qgetenv("EAPO_GALLERY_LANG").trimmed();
	if (!galleryLang.isEmpty())
	{
		QTranslator* translator = new QTranslator(qApp);
		if (translator->load(QStringLiteral(":/translations/Editor_") + QString::fromLatin1(galleryLang)))
			QCoreApplication::installTranslator(translator);
		else
			qWarning("SkinGallery: no catalog for EAPO_GALLERY_LANG=%s", galleryLang.constData());
	}

	// The reference cards probe target files; the gallery provides synthetic
	// ones and marks itself so the cards skip the audio-service ACL probe,
	// which has no meaningful answer for freshly written scratch files.
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(outDir);
	if (configPath.isEmpty())
	{
		qWarning("SkinGallery: cannot write reference target files under %s", qPrintable(outDir.absolutePath()));
		return 2;
	}

	int failures = 0;
	if (qEnvironmentVariableIsSet("EAPO_GALLERY_LEGACY"))
	{
		// Heritage mode is skin-independent; render its two dumps and exit
		// through the same no-teardown path below.
		failures += renderHeritage(outDir, configPath);
		const int status = failures == 0 ? 0 : 1;
		std::fflush(nullptr);
		std::_Exit(status);
	}
	for (const QString& skinId : skinIds)
	{
		failures += renderSkin(outDir, skinId.trimmed(), configPath, true);
		failures += renderSkin(outDir, skinId.trimmed(), configPath, false);
	}

	// Self-check the shot count so a silently dropped skin, row or state fails
	// the run even when every attempted grab succeeded. galleryRows() drives the
	// row term, so adding a gallery row updates this expectation automatically
	// and no external (build.yml) count needs to be touched.
	const int extraShots = fixedScenarioShotCount();
	const int perSkinMode = static_cast<int>(galleryRows().size()) * kStatesPerRow + extraShots;
	const int expected = static_cast<int>(skinIds.size()) * 2 * perSkinMode;
	const int actual = static_cast<int>(outDir.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files).size());
	if (actual != expected)
	{
		qWarning("SkinGallery: expected %d shots (%d skins x 2 modes x (%d rows x %d + %d extras)), wrote %d",
			expected, static_cast<int>(skinIds.size()), static_cast<int>(galleryRows().size()), kStatesPerRow, extraShots, actual);
		failures++;
	}
	for (const QString& skinId : skinIds)
	{
		for (const QString& mode : { QStringLiteral("dark"), QStringLiteral("light") })
		{
			for (const GalleryScenario& scenario : galleryScenarios())
			{
				for (const QString& state : scenario.states)
				{
					const QString fileName = QStringLiteral("%1_%2_%3_%4.png")
						.arg(skinId.trimmed(), mode, scenario.id, state);
					if (!outDir.exists(fileName))
					{
						qWarning("SkinGallery: registered scenario output is missing: %s",
							qPrintable(fileName));
						failures++;
					}
				}
			}
		}
	}

	// --skin-gallery is a headless one-shot: by this point every screenshot has
	// been rendered and flushed to disk. Returning normally would unwind into the
	// QApplication / global teardown, which on the offscreen platform never
	// finishes - a leftover background resource keeps the process alive, so the
	// renders all succeed but the process hangs on exit and any driving script
	// has to time out and kill it. Nothing is left to persist, so flush the
	// diagnostic stream and exit immediately with the failure status instead.
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}
}

// Mechanical round-trip check for VST plugin data: parse a VSTPlugin line, feed
// the parsed library, opaque state and options into the real VSTPluginFilterGUI,
// call its store(), reparse the result and confirm ChunkData, parameters,
// StereoInput and the hidden Input/Output contract survive. Returns 0 on success,
// 1 on any loss.
int SkinGallery::runVstRoundTripSelfTest()
{
	struct Case { const wchar_t* name = nullptr; std::wstring params; };
	const Case cases[] = {
		{ L"chunkData", L"Library \"fake plugin.dll\" ChunkData \"QUJDREVGR0g=\"" },
		{ L"paramMap", L"Library fake.dll Gain 0.5 Mix 0.25 Width 1" },
		{ L"paramMap-quoted-name", L"Library fake.dll \"Dry/Wet\" 0.75 Output 0.5" },
		{ L"stereoInput-chunk", L"Library fake.dll StereoInput 1 ChunkData \"QUJDREVGR0g=\"" },
		{ L"stereoInput-params", L"Library fake.dll StereoInput 1 Gain 0.5" },
		{ L"busContract", L"Library fake.vst3 Input Stereo Output 7.1 Gain 0.5" },
		{ L"busContract-slotFill",
		  L"Library fake.vst3 Input 5.1 InputChannels L,R,C,-,SL,SR Output 5.1 OutputChannels L,R,C,LFE,RL,RR Gain 0.5" }
	};

	int failures = 0;
	for (const Case& c : cases)
	{
		VSTPluginFilterFactory factory;
		std::wstring command = L"VSTPlugin";
		std::wstring params = c.params;
		FilterVector filters = factory.createFilter(L"", command, params);
		if (filters.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: parse produced no filter\n", c.name);
			failures++;
			continue;
		}
		VSTPluginFilter* f0 = static_cast<VSTPluginFilter*>(filters[0].get());
		std::wstring chunk0 = f0->getChunkData();
		std::unordered_map<std::wstring, float> map0 = f0->getParamMap();
		const bool stereo0 = f0->getStereoInput();
		const std::optional<VST3BusContract> bus0 = f0->getBusContract();
		const std::vector<std::wstring> fillIn0 = f0->getInputChannels();
		const std::vector<std::wstring> fillOut0 = f0->getOutputChannels();

		VSTPluginFilterGUI gui(f0->getLibrary(), chunk0, map0, stereo0, bus0, fillIn0, fillOut0);
		QString outCommand, outParams;
		gui.store(outCommand, outParams);

		std::wstring command2 = outCommand.toStdWString();
		std::wstring params2 = outParams.toStdWString();
		FilterVector filters2 = factory.createFilter(L"", command2, params2);
		if (filters2.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: re-parse produced no filter (params='%ls')\n", c.name, params2.c_str());
			failures++;
			continue;
		}
		VSTPluginFilter* f1 = static_cast<VSTPluginFilter*>(filters2[0].get());
		std::wstring chunk1 = f1->getChunkData();
		std::unordered_map<std::wstring, float> map1 = f1->getParamMap();
		const bool stereo1 = f1->getStereoInput();
		const std::optional<VST3BusContract> bus1 = f1->getBusContract();
		const bool sameBusContract = bus0.has_value() == bus1.has_value()
			&& (!bus0 || (bus0->input == bus1->input && bus0->output == bus1->output));
		const bool sameSlotFill = fillIn0 == f1->getInputChannels() && fillOut0 == f1->getOutputChannels();
		bool ok = (chunk0 == chunk1) && (map0 == map1) && (stereo0 == stereo1) && sameBusContract && sameSlotFill;
		if (!ok)
		{
			failures++;
			fprintf(stderr, "[VST selftest] %ls: LOSS. chunk %ls->%ls, params %zu->%zu, stereoInput %d->%d, bus %d->%d, fill %zu/%zu->%zu/%zu\n",
				c.name, chunk0.c_str(), chunk1.c_str(), map0.size(), map1.size(), stereo0 ? 1 : 0,
				stereo1 ? 1 : 0, bus0 ? 1 : 0, bus1 ? 1 : 0,
				fillIn0.size(), fillOut0.size(), f1->getInputChannels().size(), f1->getOutputChannels().size());
			for (auto& kv : map0)
			{
				auto it = map1.find(kv.first);
				if (it == map1.end())
					fprintf(stderr, "    dropped param '%ls'=%g\n", kv.first.c_str(), kv.second);
				else if (it->second != kv.second)
					fprintf(stderr, "    param '%ls' %g -> %g\n", kv.first.c_str(), kv.second, it->second);
			}
		}
		else
		{
			fprintf(stderr, "[VST selftest] %ls: OK (chunk len %zu, %zu params preserved)\n",
				c.name, chunk0.size(), map0.size());
		}
	}

	fprintf(stderr, "[VST selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

int SkinGallery::runVstFillSelfTest()
{
	// A 7.1 endpoint (the layout Windows offers for the field setup); the
	// row itself negotiates Stereo in / 5.1 out like a stereo upmixer. Each
	// case lists the rows above the VST row and the channels its fill menus
	// must offer: the device set, a Channel row's narrowed selection, and
	// the device set again when that Channel row is powered off (the engine
	// skips commented lines).
	struct Case { const char* name = nullptr; QList<QString> rowsAbove; QStringList expected; };
	const QStringList deviceChannels = {
		QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"),
		QStringLiteral("RL"), QStringLiteral("RR"), QStringLiteral("SL"), QStringLiteral("SR")};
	const Case cases[] = {
		{ "device", {}, deviceChannels },
		{ "channel-narrowed", { QStringLiteral("Channel: L R") },
		  { QStringLiteral("L"), QStringLiteral("R") } },
		{ "channel-commented", { QStringLiteral("# Channel: L R") }, deviceChannels }
	};
	const QString vstLine = QStringLiteral("VSTPlugin: Library example.vst3 Input Stereo Output 5.1");

	int failures = 0;
	for (const Case& c : cases)
	{
		for (int modeIndex = 0; modeIndex < 2; modeIndex++)
		{
			const bool legacy = modeIndex == 1;
			const QByteArray label = QByteArray(c.name) + (legacy ? "/legacy" : "/cards");
			QScrollArea scrollArea;
			scrollArea.setWidgetResizable(true);
			scrollArea.resize(960, 720);
			FilterTable* table = new FilterTable();
			table->setRenderMode(legacy ? FilterTable::LegacyRows : FilterTable::ModernCards);
			scrollArea.setWidget(table);
			QList<std::shared_ptr<AbstractAPOInfo>> outputDevices, inputDevices;
			galleryDevices(outputDevices, inputDevices);
			std::shared_ptr<AbstractAPOInfo> device =
				std::make_shared<GalleryAPOInfo>(L"Speakers", L"Example Audio", false, true, 8, 0x63F);
			table->updateDeviceAndChannelMask(device, 0x63F);
			table->initialize(&scrollArea, outputDevices, inputDevices);
			QList<QString> lines = c.rowsAbove;
			lines.append(vstLine);
			table->setLines(QString(), lines);
			table->updateGuis();
			scrollArea.show();
			QApplication::processEvents();

			// Both presentations report the menu of every slot; the cards
			// path through the rail cells, the legacy row through its combos
			// after the fold latch is opened (a defaulted fill starts folded,
			// exactly as the field report found it).
			QList<QStringList> menus;
			if (legacy)
			{
				VSTPluginFilterGUI* gui = table->findChild<VSTPluginFilterGUI*>();
				if (gui == nullptr)
				{
					fprintf(stderr, "[VST fill selftest] %s: no VSTPluginFilterGUI row\n", label.constData());
					failures++;
					continue;
				}
				QCheckBox* latch = gui->findChild<QCheckBox*>();
				if (latch != nullptr && !latch->isChecked())
					latch->setChecked(true);
				QApplication::processEvents();
				for (QComboBox* combo : gui->findChildren<QComboBox*>())
				{
					if (!combo->objectName().isEmpty())
						continue;
					QStringList items;
					for (int i = 0; i < combo->count(); i++)
						items.append(combo->itemData(i).toString());
					menus.append(items);
				}
			}
			else
			{
				for (const VSTSlotFillCell* cell : table->findChildren<VSTSlotFillCell*>())
					menus.append(cell->channelChoices());
			}

			// Stereo in + 5.1 out: two input slots and six output slots.
			if (menus.size() != 8)
			{
				fprintf(stderr, "[VST fill selftest] %s: expected 8 slot menus, found %d\n",
					label.constData(), int(menus.size()));
				failures++;
				continue;
			}
			bool ok = true;
			for (const QStringList& menu : menus)
			{
				QStringList channels = menu;
				channels.removeAll(QStringLiteral("-"));
				if (channels != c.expected)
					ok = false;
			}
			if (!ok)
			{
				failures++;
				fprintf(stderr, "[VST fill selftest] %s: expected %s, menus offer:\n", label.constData(),
					c.expected.join(QLatin1Char(',')).toUtf8().constData());
				for (const QStringList& menu : menus)
					fprintf(stderr, "    %s\n", menu.join(QLatin1Char(',')).toUtf8().constData());
			}
			else
			{
				fprintf(stderr, "[VST fill selftest] %s: OK (%s)\n", label.constData(),
					c.expected.join(QLatin1Char(',')).toUtf8().constData());
			}
		}
	}
	fprintf(stderr, "[VST fill selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

int SkinGallery::runRoutingEditTest()
{
	qWarning("RoutingEditTest: starting");
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("RoutingEditTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("RoutingEditTest: cannot write reference target files");
		return 2;
	}

	int failures = 0;
	const auto settle = []() {
		QApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();
	};
	const auto check = [&failures](bool ok, const QString& what) {
		if (!ok)
		{
			qWarning("RoutingEditTest: %s", qPrintable(what));
			failures++;
		}
		return ok;
	};
	const auto lineText = [](QScrollArea& scrollArea, int row) {
		const FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		return table != nullptr && row < table->documentItems().size()
			? table->documentItems().at(row)->text.trimmed() : QString();
	};
	const auto liveView = [](FilterCardRow* row) -> RoutingView* {
		// MultiConvolution rebuilds its view after file metadata arrives and
		// hides the superseded one until deleteLater lands.
		RoutingView* view = nullptr;
		for (RoutingView* candidate : row->findChildren<RoutingView*>())
			if (candidate->isVisible())
				view = candidate;
		return view;
	};
	const auto pressEnter = [](QWidget* editor) {
		QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(editor, &enterPress);
	};
	// The source editor's own accounting: text plus the sheet's padding and
	// hairline must fit the field, or the token is unreadable.
	const auto editorFits = [](QLineEdit* editor) {
		return editor->fontMetrics().horizontalAdvance(editor->text()) + 14 <= editor->width()
			&& editor->height() >= editor->fontMetrics().height() + 4;
	};
	// Retype the open source editor's token and commit it with Enter; the
	// editor must be the focus widget the view just opened.
	const auto retype = [&](RoutingView* view, const QString& target, const QString& token) -> QLineEdit* {
		view->grab();
		view->galleryShowcase(QStringLiteral("editSource:") + target);
		QApplication::processEvents();
		QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
		if (editor == nullptr || !editor->isVisible())
			return nullptr;
		editor->setText(token);
		pressEnter(editor);
		settle();
		return editor;
	};

	auto stereo = std::make_shared<GalleryAPOInfo>(
		L"Speakers", L"Example Audio", false, true, 2, 0x3);

	// Part 1: the minimal step list over an empty Copy on a stereo endpoint.
	{
		SkinManager::instance()->applySkin(QStringLiteral("minimal"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy:") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? rows[0]->findChild<RoutingView*>() : nullptr;
		if (!check(view != nullptr, QStringLiteral("minimal Copy row has no routing view")))
		{
			failures += 10;
		}
		else
		{
			// Two virtual channels typed at the prompt, the way an upmix is
			// started: they must be offered as sources from then on.
			for (const QString& name : { QStringLiteral("VL"), QStringLiteral("VR") })
			{
				view->galleryShowcase(QStringLiteral("addChannel"));
				QApplication::processEvents();
				QLineEdit* prompt = qobject_cast<QLineEdit*>(QApplication::focusWidget());
				if (!check(prompt != nullptr, QStringLiteral("the add-channel prompt did not take focus")))
					break;
				check(editorFits(prompt) || prompt->text().isEmpty(),
					QStringLiteral("the add-channel prompt clips its text"));
				prompt->setText(name);
				check(editorFits(prompt), QStringLiteral("the add-channel prompt clips '%1'").arg(name));
				pressEnter(prompt);
				settle();
			}
			const QStringList before = view->sourceCandidates(QStringLiteral("L"));
			check(before == QStringList({ QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("VL"), QStringLiteral("VR") }),
				QStringLiteral("first source hint for L is '%1' (expected L R VL VR)").arg(before.join(' ')));

			check(view->connectSource(QStringLiteral("L"), QStringLiteral("L")),
				QStringLiteral("connecting L to L was refused"));
			settle();
			check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=L"),
				QStringLiteral("after L=L the line is '%1'").arg(lineText(scrollArea, 0)));
			check(rows[0]->findChild<RoutingView*>() == view,
				QStringLiteral("the Copy row rebuilt its routing view on an edit"));
			// The report: from the second source on, the hint shrank to R.
			const QStringList after = view->sourceCandidates(QStringLiteral("L"));
			check(after == QStringList({ QStringLiteral("R"), QStringLiteral("VL"), QStringLiteral("VR") }),
				QStringLiteral("second source hint for L is '%1' (expected R VL VR)").arg(after.join(' ')));

			// The source editor: readable, and the token grammar of the line.
			view->grab();
			view->galleryShowcase(QStringLiteral("editSource:L"));
			QApplication::processEvents();
			QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
			if (check(editor != nullptr && editor->isVisible(), QStringLiteral("the source editor did not open")))
			{
				check(editor->text() == QStringLiteral("L"),
					QStringLiteral("the source editor holds '%1' (expected L)").arg(editor->text()));
				check(editorFits(editor),
					QStringLiteral("the source editor (%1x%2) clips '%3'")
					.arg(editor->width()).arg(editor->height()).arg(editor->text()));
				editor->setText(QStringLiteral("-0.000dB*LFE"));
				check(editorFits(editor),
					QStringLiteral("the source editor (%1 px) clips a full token").arg(editor->width()));
				editor->setText(QStringLiteral("0.5*R"));
				pressEnter(editor);
				settle();
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=0.5*R"),
					QStringLiteral("after retyping 0.5*R the line is '%1'").arg(lineText(scrollArea, 0)));
			}
			if (retype(view, QStringLiteral("L"), QStringLiteral("INV")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=-1.0*R"),
					QStringLiteral("a bare INV keeps the channel: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for INV"));
			if (retype(view, QStringLiteral("L"), QStringLiteral("VL")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=VL"),
					QStringLiteral("a bare channel is unity: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for VL"));
			if (retype(view, QStringLiteral("L"), QStringLiteral("L+R")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=VL"),
					QStringLiteral("an unreadable token must leave the line: it is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for L+R"));
			if (retype(view, QStringLiteral("L"), QString()) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy:"),
					QStringLiteral("an emptied token removes the source: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for removal"));
		}
	}

	// Part 2: MultiConvolution over the 4-channel BRIR - a port retyped in
	// the same editor is the port, not a gain.
	{
		SkinManager::instance()->applySkin(QStringLiteral("minimal"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("MultiConvolution: L=0 R=0 brir.wav") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? liveView(rows[0]) : nullptr;
		if (!check(view != nullptr, QStringLiteral("minimal MultiConvolution row has no routing view")))
		{
			failures += 4;
		}
		else
		{
			const QStringList hint = view->sourceCandidates(QStringLiteral("R"));
			check(hint == QStringList({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3") }),
				QStringLiteral("MultiConvolution source hint for R is '%1' (expected 1 2 3)").arg(hint.join(' ')));
			view->grab();
			view->galleryShowcase(QStringLiteral("editSource:R"));
			QApplication::processEvents();
			QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
			if (check(editor != nullptr && editor->isVisible(), QStringLiteral("the MultiConvolution source editor did not open")))
			{
				check(editor->text() == QStringLiteral("0"),
					QStringLiteral("the MultiConvolution source editor holds '%1' (expected 0)").arg(editor->text()));
				check(editorFits(editor),
					QStringLiteral("the MultiConvolution source editor (%1x%2) clips '%3'")
					.arg(editor->width()).arg(editor->height()).arg(editor->text()));
				editor->setText(QStringLiteral("1"));
				pressEnter(editor);
				settle();
				check(lineText(scrollArea, 0) == QStringLiteral("MultiConvolution: L=0 R=1 brir.wav"),
					QStringLiteral("after retyping port 1 the line is '%1'").arg(lineText(scrollArea, 0)));
			}
		}
	}

	// Part 3: ALL releases on the next pick, in every skin, on both cards.
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		SkinManager::instance()->applySkin(skin->id(), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Channel: ALL"), QStringLiteral("Device: all") }, stereo, 0x3);
		if (!check(rows.size() == 2, QStringLiteral("%1: expected 2 rows").arg(name)))
			continue;

		QToolButton* seat = nullptr;
		const QToolButton* allChannels = nullptr;
		for (QToolButton* button : rows[0]->findChildren<QToolButton*>())
		{
			if (button->objectName() != QStringLiteral("ChannelChip"))
				continue;
			if (button->property("allChannels").toBool())
				allChannels = button;
			else if (button->text() == QStringLiteral("R"))
				seat = button;
		}
		if (check(seat != nullptr && allChannels != nullptr, QStringLiteral("%1: Channel card chips not found").arg(name)))
		{
			check(allChannels->isChecked(), QStringLiteral("%1: ALL is not checked for 'Channel: ALL'").arg(name));
			check(seat->isEnabled(), QStringLiteral("%1: the R chip is inert while ALL is on").arg(name));
			seat->click();
			settle();
			check(lineText(scrollArea, 0) == QStringLiteral("Channel: R"),
				QStringLiteral("%1: after picking R the line is '%2'").arg(name, lineText(scrollArea, 0)));
			check(!allChannels->isChecked(), QStringLiteral("%1: ALL stayed checked after the pick").arg(name));
			check(seat->isChecked(), QStringLiteral("%1: the picked R chip is not checked").arg(name));
		}

		QToolButton* device = nullptr;
		const QToolButton* allDevices = nullptr;
		for (QToolButton* button : rows[1]->findChildren<QToolButton*>())
		{
			if (button->objectName() != QStringLiteral("DeviceChip"))
				continue;
			if (button->property("allDevices").toBool())
				allDevices = button;
			else if (device == nullptr && button->isVisible())
				device = button;
		}
		if (check(device != nullptr && allDevices != nullptr, QStringLiteral("%1: Device card chips not found").arg(name)))
		{
			check(allDevices->isChecked(), QStringLiteral("%1: All devices is not checked for 'Device: all'").arg(name));
			check(device->isEnabled(), QStringLiteral("%1: the device chip is inert while all is on").arg(name));
			device->click();
			settle();
			const QString line = lineText(scrollArea, 1);
			check(line.startsWith(QStringLiteral("Device: ")) && line != QStringLiteral("Device: all") && line.size() > 8,
				QStringLiteral("%1: after picking a device the line is '%2'").arg(name, line));
			check(!allDevices->isChecked(), QStringLiteral("%1: All devices stayed checked after the pick").arg(name));
			check(device->isChecked(), QStringLiteral("%1: the picked device chip is not checked").arg(name));
		}
	}

	qWarning("RoutingEditTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

int SkinGallery::runScrollBench()
{
	qWarning("ScrollBench: starting");
	QTemporaryDir scratch;
	if (!scratch.isValid())
		return 2;
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
		return 2;

	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	for (ISkin* skin : Skins::all())
	{
		QScrollArea scrollArea;
		// A maximized QHD editor: the card column is ~2500 px wide.
		scrollArea.resize(2560, 1300);
		SkinManager::instance()->applySkin(skin->id(), true);
		buildRows(scrollArea, configPath, lines);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
			continue;
		table->openConfig(QString());
		QApplication::processEvents();

		QScrollBar* bar = scrollArea.verticalScrollBar();
		bar->setValue(0);
		QApplication::processEvents();
		// Force real rasterization per step: grab() renders the viewport
		// through the same paint path the screen would use.
		QElapsedTimer timer;
		timer.start();
		int steps = 0;
		for (int value = 0; value <= bar->maximum() && steps < 40; value += 120, steps++)
		{
			bar->setValue(value);
			QApplication::processEvents();
			scrollArea.viewport()->grab();
		}
		const qint64 elapsed = timer.elapsed();
		qWarning("ScrollBench: %s %d steps in %lld ms (%.1f ms/step)",
			qPrintable(skin->id()), steps, static_cast<long long>(elapsed),
			steps > 0 ? double(elapsed) / steps : 0.0);
	}
	std::fflush(nullptr);
	std::_Exit(0);
}

int SkinGallery::runPowerToggleTest()
{
	qWarning("PowerToggleTest: starting");
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("PowerToggleTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("PowerToggleTest: cannot write reference target files");
		return 2;
	}

	// Gain-less biquads (the field report) and full-grammar controls must
	// keep a real editor through load and an off/on round trip; a peaking
	// line missing its gain is one the ENGINE rejects ("no gain given"),
	// so its raw presentation is correct and only the text round trip is
	// held. Every line must survive the toggle byte-identically.
	struct Case { QString line; bool editor = true; };
	const QList<Case> cases = {
		{ QStringLiteral("Filter: ON NO Fc 800 Hz"), true },
		{ QStringLiteral("Filter: ON AP Fc 900 Hz BW Oct 1"), true },
		{ QStringLiteral("Filter: ON LP Fc 5000 Hz"), true },
		{ QStringLiteral("Filter: ON HPQ Fc 80 Hz Q 0.5"), true },
		{ QStringLiteral("Filter: ON BP Fc 1000 Hz Q 2"), true },
		{ QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71"), true },
		{ QStringLiteral("Filter: ON PK Fc 1000 Hz Q 1"), false },
		{ QStringLiteral("Preamp: -3 dB"), true }
	};
	QList<QString> lines;
	for (const Case& c : cases)
		lines.append(c.line);

	int failures = 0;
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		SkinManager::instance()->applySkin(skin->id(), true);
		buildRows(scrollArea, configPath, lines);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("PowerToggleTest: %s table construction failed", qPrintable(name));
			failures++;
			continue;
		}
		table->openConfig(QString());

		const auto rowWidget = [table](int index) -> FilterCardRow* {
			QList<FilterCardRow*> rows = table->findChildren<FilterCardRow*>(
				QString(), Qt::FindDirectChildrenOnly);
			std::sort(rows.begin(), rows.end(), [](FilterCardRow* a, FilterCardRow* b) {
				return a->y() < b->y();
			});
			return index < rows.size() ? rows[index] : nullptr;
		};
		const auto powerButton = [](FilterCardRow* row) -> QToolButton* {
			for (QToolButton* button : row->findChildren<QToolButton*>())
			{
				if (button->toolTip() == QStringLiteral("Enable or comment out this command"))
					return button;
			}
			return nullptr;
		};
		const auto settle = []() {
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();
		};

		for (int i = 0; i < lines.size(); i++)
		{
			const QString original = table->documentItems().at(i)->text;
			if ((table->documentItems().at(i)->gui != nullptr) != cases[i].editor)
			{
				// The load-time half of the regression this gate pins: the
				// audit #275 B4 policy extraction dropped the card-to-chain
				// fallthrough and every plain biquad loaded as a raw row.
				qWarning("PowerToggleTest: %s row %d editor presence %d at load (expected %d)",
					qPrintable(name), i + 1,
					table->documentItems().at(i)->gui != nullptr ? 1 : 0, cases[i].editor ? 1 : 0);
				failures++;
			}
			for (int phase = 0; phase < 2; phase++)
			{
				FilterCardRow* row = rowWidget(i);
				QToolButton* button = row != nullptr ? powerButton(row) : nullptr;
				if (button == nullptr)
				{
					qWarning("PowerToggleTest: %s row %d lost its power button in phase %d",
						qPrintable(name), i + 1, phase);
					failures++;
					break;
				}
				button->setChecked(phase == 1);
				settle();
				const QString text = table->documentItems().at(i)->text;
				const QString expected = phase == 0
					? QStringLiteral("# ") + original : original;
				if (text != expected)
				{
					qWarning("PowerToggleTest: %s row %d phase %d text '%s' (expected '%s')",
						qPrintable(name), i + 1, phase,
						qPrintable(text), qPrintable(expected));
					failures++;
				}
			}
			FilterCardRow* row = rowWidget(i);
			const bool hasGui = table->documentItems().at(i)->gui != nullptr;
			if (hasGui != cases[i].editor)
			{
				qWarning("PowerToggleTest: %s row %d editor presence %d after the toggle (expected %d)",
					qPrintable(name), i + 1, hasGui ? 1 : 0, cases[i].editor ? 1 : 0);
				failures++;
			}
			Q_UNUSED(row);
		}
	}
	qWarning("PowerToggleTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

bool SkinGallery::armAnalysisLayoutProbe(MainWindow& window, const QString& screenshotPath)
{
	QDockWidget* dock = window.findChild<QDockWidget*>(QStringLiteral("analysisDockWidget"));
	if (dock == nullptr)
	{
		fprintf(stderr, "Analysis layout test: required dock is missing\n");
		return false;
	}

	window.showNormal();
	window.resize(1024, 768);
	dock->show();
		QTimer::singleShot(750, &window, [&window, dock, screenshotPath]() {
		fprintf(stderr,
			"Analysis layout target: platform=%s style=%s dpr=%.2f locale=%s skin=%s dark=%d\n",
			qPrintable(QGuiApplication::platformName()), qPrintable(qApp->style()->objectName()),
			window.devicePixelRatioF(), qPrintable(QLocale().name()),
			qPrintable(SkinManager::instance()->currentSkinId()),
			SkinManager::instance()->isDark() ? 1 : 0);
		QWidget* controls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar"));
		QWidget* graph = window.findChild<QWidget*>(QStringLiteral("ModernAnalysisGraph"));
		QBoxLayout* layout = window.findChild<QBoxLayout*>(QStringLiteral("analysisDockLayout"));
		const int centralWidth = window.centralWidget()->width();
		const int dockWidth = dock->width();
		const int controlsWidth = controls != nullptr ? controls->width() : -1;
		const int graphWidth = graph != nullptr ? graph->width() : -1;
		const bool centralProtected = centralWidth * 100 >= window.width() * 55;
		const bool dockBounded = dockWidth * 100 <= window.width() * 45;
		const bool graphUsable = graphWidth >= 300;
		const bool controlsFill = controlsWidth >= graphWidth - 2;
		const bool stacked = layout != nullptr && layout->direction() == QBoxLayout::TopToBottom;

		bool screenshotSaved = screenshotPath.isEmpty();
		if (!screenshotPath.isEmpty())
		{
			QFileInfo screenshotInfo(screenshotPath);
			QDir().mkpath(screenshotInfo.absolutePath());
			const QPixmap shot = window.grab();
			screenshotSaved = shot.save(screenshotPath);
		}

		const bool rightPass = centralProtected && dockBounded && graphUsable
			&& controlsFill && stacked && screenshotSaved;
		fprintf(stderr,
			"Analysis layout test (right): window=%dx%d central=%d dock=%d controls=%d graph=%d "
			"centralProtected=%d dockBounded=%d graphUsable=%d controlsFill=%d stacked=%d screenshot=%d\n",
			window.width(), window.height(), centralWidth, dockWidth, controlsWidth, graphWidth,
			centralProtected ? 1 : 0, dockBounded ? 1 : 0, graphUsable ? 1 : 0,
			controlsFill ? 1 : 0, stacked ? 1 : 0, screenshotSaved ? 1 : 0);

		const bool bottomRequested = QMetaObject::invokeMethod(&window,
			"on_graphPositionComboBox_currentIndexChanged", Qt::DirectConnection,
			Q_ARG(int, 1));
		QTimer::singleShot(250, &window, [&window, dock, rightPass, bottomRequested]() {
			QWidget* bottomControls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar"));
			QBoxLayout* bottomLayout = window.findChild<QBoxLayout*>(QStringLiteral("analysisDockLayout"));
			const bool bottomArea = window.dockWidgetArea(dock) == Qt::BottomDockWidgetArea;
			const bool horizontal = bottomLayout != nullptr
				&& bottomLayout->direction() == QBoxLayout::LeftToRight;
			const bool compactControls = bottomControls != nullptr
				&& bottomControls->maximumWidth() < QWIDGETSIZE_MAX
				&& bottomControls->width() <= bottomControls->maximumWidth();
			fprintf(stderr,
				"Analysis layout test (bottom): area=%d horizontal=%d compactControls=%d\n",
				bottomArea ? 1 : 0, horizontal ? 1 : 0, compactControls ? 1 : 0);
			const bool bottomPass = bottomRequested && bottomArea
				&& horizontal && compactControls;
			const bool rightRestoreRequested = QMetaObject::invokeMethod(&window,
				"on_graphPositionComboBox_currentIndexChanged", Qt::DirectConnection,
				Q_ARG(int, 2));
			QTimer::singleShot(250, &window,
				[&window, dock, rightPass, bottomPass, rightRestoreRequested]() {
					QWidget* restoredControls = window.findChild<QWidget*>(
						QStringLiteral("analysisControlBar"));
					QWidget* restoredGraph = window.findChild<QWidget*>(
						QStringLiteral("ModernAnalysisGraph"));
					QBoxLayout* restoredLayout = window.findChild<QBoxLayout*>(
						QStringLiteral("analysisDockLayout"));
					const bool rightRestored = rightRestoreRequested
						&& window.dockWidgetArea(dock) == Qt::RightDockWidgetArea
						&& restoredLayout != nullptr
						&& restoredLayout->direction() == QBoxLayout::TopToBottom
						&& restoredControls != nullptr && restoredGraph != nullptr
						&& restoredControls->width() >= restoredGraph->width() - 2;
					fprintf(stderr, "Analysis layout test (right restored): pass=%d\n",
						rightRestored ? 1 : 0);
					const int exitCode = rightPass && bottomPass && rightRestored ? 0 : 1;
					bool holdOk = false;
					const int requestedHold = qEnvironmentVariableIntValue(
						"EAPO_ANALYSIS_LAYOUT_HOLD_MS", &holdOk);
					const int holdMs = holdOk ? qBound(0, requestedHold, 30000) : 0;
					if (holdMs > 0)
						QTimer::singleShot(holdMs, &window,
							[exitCode]() { QCoreApplication::exit(exitCode); });
					else
						QCoreApplication::exit(exitCode);
				});
		});
	});
	return true;
}

bool SkinGallery::armVstPanelFeedProbe(MainWindow& window, const QString& durationValue)
{
	bool durationOk = false;
	int durationMs = durationValue.toInt(&durationOk);
	if (!durationOk || durationMs <= 0)
		durationMs = 8000;

	window.showNormal();
	// Pin the window to the primary screen: a restored geometry on another
	// monitor would divorce the screen grab below from the widget rects.
	QScreen* primary = QGuiApplication::primaryScreen();
	window.setGeometry(QRect(primary->availableGeometry().topLeft() + QPoint(60, 60),
		QSize(1280, 900)));
	window.raise();
	window.activateWindow();

	// Let the configuration finish loading before the embed request.
	QTimer::singleShot(1500, &window, [&window, durationMs]() {
		// The session may have restored other configuration tabs; only the
		// active tab's widgets are visible, and the probe file was loaded
		// last, so the visible VST card is the probe's card.
		VSTCardEditor* card = nullptr;
		const QList<VSTCardEditor*> cards = window.findChildren<VSTCardEditor*>();
		for (VSTCardEditor* candidate : cards)
			if (candidate->isVisible())
				card = candidate;
		if (card == nullptr)
		{
			fprintf(stderr, "VstPanelFeedProbe: no visible VST card (%d total)\n",
				int(cards.size()));
			std::fflush(nullptr);
			std::_Exit(1);
		}
		if (!QMetaObject::invokeMethod(card, "embedToggled", Qt::DirectConnection, Q_ARG(bool, true)))
		{
			fprintf(stderr, "VstPanelFeedProbe: embed request failed\n");
			std::fflush(nullptr);
			std::_Exit(1);
		}

		// A settle pause for the plug-in view to attach and paint its first
		// frame, then the sampling run.
		QTimer::singleShot(2000, card, [card, durationMs]() {
			struct SampleState
			{
				QImage previous;
				QVector<double> diffs;
			};
			auto state = std::make_shared<SampleState>();
			const int intervalMs = 250;
			const int totalSamples = qMax(4, durationMs / intervalMs);
			QTimer* sampler = new QTimer(card);
			sampler->setInterval(intervalMs);
			QObject::connect(sampler, &QTimer::timeout, card, [card, state, totalSamples, sampler]() {
				// Composited screen pixels, because the plug-in renders into
				// native child windows QWidget::grab cannot see. grabWindow
				// offsets are relative to the screen, not the virtual desktop,
				// and the screen is the one actually containing the card.
				const QPoint globalTopLeft = card->mapToGlobal(QPoint(0, 0));
				QScreen* screen = QGuiApplication::screenAt(
					globalTopLeft + QPoint(card->width() / 2, card->height() / 2));
				if (screen == nullptr)
					screen = QGuiApplication::primaryScreen();
				const QPoint topLeft = globalTopLeft - screen->geometry().topLeft();
				const QPixmap shot = screen->grabWindow(0, topLeft.x(), topLeft.y(),
					card->width(), card->height());
				QImage image = shot.toImage().convertToFormat(QImage::Format_RGB32);
				if (state->previous.isNull())
					fprintf(stderr, "VstPanelFeedProbe: card=%dx%d at %d,%d image=%dx%d screen=%s\n",
						card->width(), card->height(), topLeft.x(), topLeft.y(),
						image.width(), image.height(), qPrintable(screen->name()));
				if (!state->previous.isNull() && state->previous.size() == image.size())
				{
					qint64 total = 0;
					qint64 sampled = 0;
					for (int y = 0; y < image.height(); y += 2)
					{
						const QRgb* current = reinterpret_cast<const QRgb*>(image.constScanLine(y));
						const QRgb* previous = reinterpret_cast<const QRgb*>(state->previous.constScanLine(y));
						for (int x = 0; x < image.width(); x += 2)
						{
							total += qAbs(qRed(current[x]) - qRed(previous[x]))
								+ qAbs(qGreen(current[x]) - qGreen(previous[x]))
								+ qAbs(qBlue(current[x]) - qBlue(previous[x]));
							sampled++;
						}
					}
					state->diffs.append(sampled > 0 ? double(total) / double(sampled) : 0.0);
				}
				state->previous = image;
				if (state->diffs.size() < totalSamples)
					return;

				sampler->stop();
				int activeFrames = 0;
				double maxDiff = 0.0;
				double sum = 0.0;
				QString series;
				for (double diff : state->diffs)
				{
					// A meter strip moving inside a mostly static card keeps
					// the per-sampled-pixel mean small; 0.05 sits well above
					// the measured no-feed noise floor (0.02) and well below
					// the measured feed signal (0.06-0.37).
					if (diff > 0.05)
						activeFrames++;
					maxDiff = qMax(maxDiff, diff);
					sum += diff;
					series += QString::number(diff, 'f', 2) + QLatin1Char(' ');
				}
				const double meanDiff = sum / state->diffs.size();
				// LIVE when a solid share of the sampled intervals moved: a
				// one-off repaint cannot fake a meter, while meter ballistics
				// legitimately hold still between peaks even on a modulated
				// signal.
				const bool live = activeFrames * 4 >= state->diffs.size();
				fprintf(stderr,
					"VstPanelFeedProbe: feedDisabled=%d frames=%d activeFrames=%d meanDiff=%.2f maxDiff=%.2f verdict=%s\n",
					qEnvironmentVariableIsSet("EAPO_DISABLE_PANEL_FEED") ? 1 : 0,
					int(state->diffs.size()), activeFrames, meanDiff, maxDiff, live ? "LIVE" : "STATIC");
				fprintf(stderr, "VstPanelFeedProbe: series=%s\n", qPrintable(series.trimmed()));
				std::fflush(nullptr);
				std::_Exit(0);
			});
			sampler->start();
		});
	});
	return true;
}

// ---------------------------------------------------------------------------
// --skin-metrics-probe (diagnostic): shrink the analysis dock, then walk the
// five skins on the live MainWindow and report the dock's size/minimums and
// which physical font face serves Korean text in the chrome. Not a gate.
// ---------------------------------------------------------------------------

#include <QFontDatabase>
#include <QFontInfo>
#include <QRawFont>
#include <QAction>

namespace
{
QAction* findSkinAction(const MainWindow& window, const QString& skinId)
{
	for (QAction* action : window.findChildren<QAction*>())
		if (action->isCheckable() && action->data().toString() == skinId)
			return action;
	return nullptr;
}

void reportFontDatabase()
{
	for (const QString& family : { QStringLiteral("Pretendard"), QStringLiteral("Pretendard Variable"),
		QStringLiteral("EAPO Sans KR"), QStringLiteral("EAPO Sans"), QStringLiteral("EAPO Mono"), QStringLiteral("EAPO Mono K"), QStringLiteral("Malgun Gothic"),
		QStringLiteral("Noto Sans KR"), QStringLiteral("Noto Sans") })
	{
		fprintf(stderr, "  family %s: has=%d private=%d korean=%d styles=",
			qPrintable(family), QFontDatabase::hasFamily(family) ? 1 : 0,
			QFontDatabase::isPrivateFamily(family) ? 1 : 0,
			QFontDatabase::writingSystems(family).contains(QFontDatabase::Korean) ? 1 : 0);
		for (const QString& style : QFontDatabase::styles(family))
			fprintf(stderr, "[%s w%d] ", qPrintable(style), QFontDatabase::weight(family, style));
		fprintf(stderr, "\n");
	}
}

void reportFontMatrix()
{
	const QList<QStringList> lists = {
		{ QStringLiteral("Pretendard") },
		{ QStringLiteral("EAPO Sans KR") },
		{ QStringLiteral("EAPO Sans"), QStringLiteral("EAPO Sans KR") },
		{ QStringLiteral("EAPO Mono"), QStringLiteral("EAPO Mono K"), QStringLiteral("EAPO Sans KR") },
		{ QStringLiteral("EAPO Sans"), QStringLiteral("EAPO Sans KR"), QStringLiteral("Noto Sans KR"),
			QStringLiteral("Noto Sans"), QStringLiteral("Malgun Gothic"), QStringLiteral("Microsoft YaHei"),
			QStringLiteral("sans-serif") }
	};
	for (const QStringList& list : lists)
	{
		for (int weight : { 400, 500, 600, 700 })
		{
			fprintf(stderr, "  matrix [%s] w=%d:", qPrintable(list.join(QLatin1Char(','))), weight);
			for (double pt : { 9.0, 10.0, 10.5, 11.0, 12.0, 14.0 })
			{
				QFont font;
				font.setFamilies(list);
				font.setWeight(QFont::Weight(weight));
				font.setPointSizeF(pt);
				const QRawFont korean = QRawFont::fromFont(font, QFontDatabase::Korean);
				fprintf(stderr, " %.1f=%s/%s", pt, qPrintable(korean.familyName()), qPrintable(korean.styleName()));
			}
			fprintf(stderr, "\n");
		}
	}
}

void reportFace(const char* label, const QWidget* widget)
{
	if (widget == nullptr)
	{
		fprintf(stderr, "    %s: (missing)\n", label);
		return;
	}
	const QFont font = widget->font();
	const QFontInfo info(font);
	const QRawFont latin = QRawFont::fromFont(font, QFontDatabase::Latin);
	const QRawFont korean = QRawFont::fromFont(font, QFontDatabase::Korean);
	fprintf(stderr, "    %s: request=[%s] w=%d pt=%.1f | info=%s w=%d | latin=%s/%s w=%d | korean=%s/%s w=%d\n",
		label, qPrintable(font.families().join(QLatin1Char(','))), int(font.weight()), font.pointSizeF(),
		qPrintable(info.family()), int(info.weight()),
		qPrintable(latin.familyName()), qPrintable(latin.styleName()), int(latin.weight()),
		qPrintable(korean.familyName()), qPrintable(korean.styleName()), int(korean.weight()));
}

void reportFonts(const MainWindow& window)
{
	reportFace("titlebar", window.findChild<QLabel*>(QStringLiteral("TitleBarText")));
	reportFace("menubar", window.findChild<QMenuBar*>());
	reportFace("cardtitle", window.findChild<QLabel*>(QStringLiteral("FilterCardTitle")));
	reportFace("formlabel", window.findChild<QLabel*>(QStringLiteral("AnalysisFormLabel")));
	QLabel probe(QStringLiteral("probe"));
	probe.ensurePolished();
	reportFace("plainlabel", &probe);
}

void reportDockMetrics(const MainWindow& window, const QDockWidget* dock)
{
	const QWidget* contents = dock->widget();
	const QWidget* controls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar"));
	const QWidget* graph = window.findChild<QWidget*>(QStringLiteral("ModernAnalysisGraph"));
	const auto sz = [](const QSize& s) { return QStringLiteral("%1x%2").arg(s.width()).arg(s.height()); };
	fprintf(stderr, "    dock: size=%s minHint=%s min=%s hint=%s\n",
		qPrintable(sz(dock->size())), qPrintable(sz(dock->minimumSizeHint())),
		qPrintable(sz(dock->minimumSize())), qPrintable(sz(dock->sizeHint())));
	if (contents != nullptr)
		fprintf(stderr, "    contents: size=%s minHint=%s min=%s\n",
			qPrintable(sz(contents->size())), qPrintable(sz(contents->minimumSizeHint())),
			qPrintable(sz(contents->minimumSize())));
	if (controls != nullptr)
	{
		fprintf(stderr, "    controls: size=%s minHint=%s hint=%s\n",
			qPrintable(sz(controls->size())), qPrintable(sz(controls->minimumSizeHint())),
			qPrintable(sz(controls->sizeHint())));
		for (const QWidget* child : controls->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (!child->isVisibleTo(controls))
				continue;
			fprintf(stderr, "      %s(%s): size=%s minHint=%s hint=%s\n",
				qPrintable(child->objectName()), child->metaObject()->className(),
				qPrintable(sz(child->size())), qPrintable(sz(child->minimumSizeHint())),
				qPrintable(sz(child->sizeHint())));
		}
	}
	if (graph != nullptr)
		fprintf(stderr, "    graph: size=%s minHint=%s min=%s hint=%s\n",
			qPrintable(sz(graph->size())), qPrintable(sz(graph->minimumSizeHint())),
			qPrintable(sz(graph->minimumSize())), qPrintable(sz(graph->sizeHint())));
}

void settle()
{
	for (int i = 0; i < 4; i++)
	{
		QCoreApplication::sendPostedEvents();
		QApplication::processEvents(QEventLoop::AllEvents, 50);
	}
}
}

bool SkinGallery::armSkinMetricsProbe(MainWindow& window)
{
	QDockWidget* dock = window.findChild<QDockWidget*>(QStringLiteral("analysisDockWidget"));
	if (dock == nullptr)
	{
		fprintf(stderr, "Skin metrics probe: required dock is missing\n");
		return false;
	}
	window.showNormal();
	window.resize(1024, 768);
	dock->show();
	QTimer::singleShot(750, &window, [&window, dock]() {
		const QString originalSkin = SkinManager::instance()->currentSkinId();
		const Qt::DockWidgetArea area = window.dockWidgetArea(dock);
		fprintf(stderr, "Skin metrics probe: platform=%s skin=%s dark=%d area=%d appfont=[%s] w=%d\n",
			qPrintable(QGuiApplication::platformName()), qPrintable(originalSkin),
			SkinManager::instance()->isDark() ? 1 : 0, int(area),
			qPrintable(QApplication::font().families().join(QLatin1Char(','))), int(QApplication::font().weight()));
		reportFontDatabase();
		reportFontMatrix();
		const bool vertical = area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea;
		window.resizeDocks({ dock }, { vertical ? 170 : 330 }, vertical ? Qt::Vertical : Qt::Horizontal);
		settle();
		fprintf(stderr, "  after shrink: dock=%dx%d\n", dock->width(), dock->height());
		for (const QString& skinId : { QStringLiteral("studio"), QStringLiteral("minimal"),
			QStringLiteral("soft"), QStringLiteral("rack"), QStringLiteral("matrix"), QStringLiteral("studio") })
		{
			QAction* action = findSkinAction(window, skinId);
			if (action == nullptr)
			{
				fprintf(stderr, "  skin %s: no menu action\n", qPrintable(skinId));
				continue;
			}
			action->trigger();
			settle();
			fprintf(stderr, "  skin %s (active=%s):\n", qPrintable(skinId),
				qPrintable(SkinManager::instance()->currentSkinId()));
			reportDockMetrics(window, dock);
			reportFonts(window);
		}
		// Second pass with the dock on the right, where the minimum WIDTH is
		// what a shrunken dock runs into.
		const QComboBox* positionCombo = window.findChild<QComboBox*>(QStringLiteral("graphPositionComboBox"));
		const int originalPosition = positionCombo != nullptr ? positionCombo->currentIndex() : -1;
		QMetaObject::invokeMethod(&window, "on_graphPositionComboBox_currentIndexChanged",
			Qt::DirectConnection, Q_ARG(int, 2));
		settle();
		window.resizeDocks({ dock }, { 330 }, Qt::Horizontal);
		settle();
		fprintf(stderr, "  right dock after shrink: dock=%dx%d area=%d\n", dock->width(), dock->height(),
			int(window.dockWidgetArea(dock)));
		for (const QString& skinId : { QStringLiteral("studio"), QStringLiteral("minimal"),
			QStringLiteral("soft"), QStringLiteral("rack"), QStringLiteral("matrix"), QStringLiteral("studio") })
		{
			QAction* action = findSkinAction(window, skinId);
			if (action == nullptr)
				continue;
			action->trigger();
			settle();
			fprintf(stderr, "  right skin %s:\n", qPrintable(skinId));
			reportDockMetrics(window, dock);
		}
		if (originalPosition >= 0)
		{
			QMetaObject::invokeMethod(&window, "on_graphPositionComboBox_currentIndexChanged",
				Qt::DirectConnection, Q_ARG(int, originalPosition));
			settle();
		}
		if (QAction* original = findSkinAction(window, originalSkin))
		{
			original->trigger();
			settle();
		}
		QCoreApplication::exit(0);
	});
	return true;
}
