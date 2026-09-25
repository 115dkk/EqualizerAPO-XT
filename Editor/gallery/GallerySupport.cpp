/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Shared scaffolding of the skin gallery and the offscreen gates: the
	representative rows, the synthetic audio endpoints, the fixture files,
	the toolbar replica and the FilterTable host (GallerySupport.h).
*/

#include "SkinGallery.h"
#include "Editor/gallery/GallerySupport.h"
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
#include "widgets/subwooferrouting/SubwooferRoutingDefaults.h"

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

namespace SkinGalleryDetail
{
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

// The gallery's fixture root: a fixed folder under %TEMP% with a constant
// name, independent of the output directory. The file dialog's "Look in" text
// and the reference cards' location lines print fixture paths, so fixtures
// under the output directory made those scenes differ whenever the gallery
// wrote somewhere else. run() recreates the folder on every run.
QDir galleryFixtureRoot()
{
	return QDir(QDir::temp().filePath(QStringLiteral("EAPO-SkinGallery-Fixtures")));
}

// Fixture tree for the file-dialog chrome shot: two folders and two matching
// configurations, every entry's timestamps pinned so the Detail view's
// name/size/date cells do not depend on when the gallery ran.
// Returns the directory the dialog opens on, or an empty string on failure.
QString buildFileDialogFixture(const QDir& fixtureRoot)
{
	QDir fixtureDir(fixtureRoot.filePath(QStringLiteral("filedialog")));
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

// Build a fresh FilterTable holding the given lines and return the card row
// widgets in line order. The table must be built after applySkin so every row
// is polished once against the active stylesheet, mirroring the real skin
// switch flow (clearRows + updateGuis).
QList<FilterCardRow*> buildRows(QScrollArea& scrollArea, const QString& configPath, const QList<QString>& lines,
	std::shared_ptr<AbstractAPOInfo> device, unsigned long channelMask)
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
	// CopyFilterGUI scene only populates through setChannelFlow(), which
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
}
