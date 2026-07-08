#include "SkinGallery.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QToolBar>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/widgets/AddCardRow.h"
#include "Editor/widgets/EqGraphView.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/FilterInsertSeam.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/TitleBar.h"
#include "Editor/widgets/UpdateToast.h"

namespace
{
struct GalleryRow
{
	QString name;
	QString line;
};

// Representative rows: a parametric filter, a shelf filter (three knobs in
// the legacy BiQuad GUI hosted by the card body), a peaking filter at 0 dB
// (the bipolar gain knob at its neutral detent, X3), the preamp card (the
// bare knob + value scrub pair - the row that shows whether a skin seats
// custom widgets directly on its surface), the reference-card rows and an
// empty Copy row (the routing editor's empty state, X6).
//
// The reference rows (Include / Convolution / MultiConvolution / VST) render
// against synthetic target files written next to a synthetic config file
// (buildReferenceFiles), so the resolved cards show their healthy named-entity
// state with a deterministic impulse-response readout. include_missing keeps
// the broken-reference transition (MISSING + Locate, AR2 X-3/X-4) in every
// skin's judged set. The VST library is intentionally unresolvable; the card
// renders its missing/not-loaded state, which doubles as the recovery-entry
// showcase. The two MultiConvolution rows cover the mapping-form card (the
// per-skin routing view over a 4-channel BRIR, both ears mapped) and the
// freshly inserted empty state; the empty one also guards the Insert path,
// where a bare "MultiConvolution:" template must still resolve to the card
// body and not fall back to an empty row. The comment and stage rows judge
// the Phase 1 cards that replaced the raw-container fallback: an in-place
// note editor and the two-lane stage card. The two device rows split the
// card's grammar over the synthetic endpoints (galleryDevices): "device"
// shows an engaged playback switch and an engaged capture well next to an
// idle endpoint (the routed/at-rest contrast every skin styles), while
// "device_all" shows the all-devices master engaged over powered-down
// endpoint chips.
QList<GalleryRow> galleryRows()
{
	return {
		{ QStringLiteral("filter"), QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71") },
		{ QStringLiteral("shelf"), QStringLiteral("Filter 2: ON HSC Fc 8000 Hz Gain -2.5 dB Q 0.71") },
		{ QStringLiteral("gain0db"), QStringLiteral("Filter 3: ON PK Fc 1000 Hz Gain 0 dB Q 1") },
		{ QStringLiteral("preamp"), QStringLiteral("Preamp: -6 dB") },
		{ QStringLiteral("include"), QStringLiteral("Include: example.txt") },
		{ QStringLiteral("include_nested"), QStringLiteral("Include: Surround\\example.txt") },
		{ QStringLiteral("include_missing"), QStringLiteral("Include: missing.txt") },
		{ QStringLiteral("vst"), QStringLiteral("VSTPlugin: Library example.dll") },
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
		// The clean-install first impression (legacy-cleanup round 3): the
		// graphic EQ card is the first thing a fresh install shows, and the two
		// raw-text shapes (a bare note line and a programmatic If command) are
		// the rows that historically rendered as nothing at all.
		{ QStringLiteral("graphiceq"), QStringLiteral("GraphicEQ: 25 -4.5; 100 -2; 1000 0; 8000 3.5; 16000 1") },
		{ QStringLiteral("text"), QStringLiteral("plain note line without a command") },
		{ QStringLiteral("iftext"), QStringLiteral("If: inputChannelCount == 2") }
	};
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
	GalleryAPOInfo(const std::wstring& connection, const std::wstring& name, bool input, bool installed)
		: connection(connection), name(name), input(input), installed(installed)
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
		return 2;
	}

	unsigned getSampleRate() const override
	{
		return 48000;
	}

	unsigned long getChannelMask() const override
	{
		return 0x3;
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

	bool isExperimental() const override
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

// renderSkin() renders, per skin and per mode: every gallery row in
// kStatesPerRow states (normal + hover from renderStates(commented=false), and
// disabled from renderStates(commented=true)), plus kExtraShotsPerSkinMode fixed
// chrome shots (picker x3, toolbar, titlebar, menubar, menu, analysis,
// addrow x2, seam, toast). run() multiplies these by skins x 2 modes to
// self-check the output count, so adding a gallery row needs no external
// count to be updated. Keep both constants in step with
// renderStates()/renderSkin() if the state set or chrome shots change.
constexpr int kStatesPerRow = 3;
constexpr int kExtraShotsPerSkinMode = 12;

// Faithful chrome replica of MainWindow's toolbar: same object names, same
// widget train, dummy data where the real one reads devices. The gallery
// judges chrome, not data, and constructing the real toolbar would drag in
// device enumeration (flaky on machines without audio endpoints).
QToolBar* buildToolbarReplica(QWidget* parent)
{
	QToolBar* toolBar = new QToolBar(parent);
	toolBar->setObjectName(QStringLiteral("MainToolBar"));
	toolBar->setMovable(false);

	QAction* actionNew = toolBar->addAction(QStringLiteral("New"));
	actionNew->setObjectName(QStringLiteral("actionNew"));
	QAction* actionOpen = toolBar->addAction(QStringLiteral("Open"));
	actionOpen->setObjectName(QStringLiteral("actionOpen"));
	QAction* actionSave = toolBar->addAction(QStringLiteral("Save"));
	actionSave->setObjectName(QStringLiteral("actionSave"));

	QWidget* spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setFixedWidth(10);
	toolBar->addWidget(spacer);

	QCheckBox* instantMode = new QCheckBox(QStringLiteral("Instant mode"));
	instantMode->setObjectName(QStringLiteral("InstantModeCheckBox"));
	instantMode->setChecked(true);
	toolBar->addWidget(instantMode);

	QLabel* dirtyBadge = new QLabel(QStringLiteral("Saved"));
	dirtyBadge->setObjectName(QStringLiteral("DirtyStatusBadge"));
	toolBar->addWidget(dirtyBadge);

	spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	toolBar->addWidget(spacer);

	QLabel* deviceLabel = new QLabel(QStringLiteral("Device"));
	deviceLabel->setObjectName(QStringLiteral("ToolBarLabel"));
	toolBar->addWidget(deviceLabel);

	SkinComboBox* deviceCombo = new SkinComboBox;
	deviceCombo->setObjectName(QStringLiteral("ToolBarComboBox"));
	deviceCombo->addItem(QStringLiteral("Default (Speakers - Example Audio)"));
	toolBar->addWidget(deviceCombo);

	spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setFixedWidth(10);
	toolBar->addWidget(spacer);

	QLabel* channelLabel = new QLabel(QStringLiteral("Channels"));
	channelLabel->setObjectName(QStringLiteral("ToolBarLabel"));
	toolBar->addWidget(channelLabel);

	SkinComboBox* channelCombo = new SkinComboBox;
	channelCombo->setObjectName(QStringLiteral("ToolBarComboBox"));
	channelCombo->addItem(QStringLiteral("7.1 surround"));
	toolBar->addWidget(channelCombo);

	SkinManager::instance()->styleMainToolbar(toolBar);
	return toolBar;
}

// Faithful replica of the analysis dock's contents: the compact settings cell
// beside the graph (feedback round, DC #1289929) with dummy readouts. Same
// object names as MainWindow so every sheet's #analysisControlBar /
// #AnalysisStatChip rules are judged; the graph is a real EqGraphView left
// empty (background and frame only, no curve data needed).
QWidget* buildAnalysisPanelReplica(QWidget* parent)
{
	QWidget* panel = new QWidget(parent);
	QHBoxLayout* dockLayout = new QHBoxLayout(panel);
	dockLayout->setContentsMargins(10, 6, 10, 10);
	dockLayout->setSpacing(8);

	QFrame* bar = new QFrame;
	bar->setObjectName(QStringLiteral("analysisControlBar"));
	bar->setAttribute(Qt::WA_StyledBackground, true);
	bar->setMaximumWidth(250);
	QGridLayout* grid = new QGridLayout(bar);
	grid->setContentsMargins(10, 8, 10, 8);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(6);

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
			spin->setRange(128, 8388608);
			spin->setValue(65536);
			grid->addWidget(spin, row, 1);
		}
		else
		{
			QComboBox* combo = new QComboBox;
			combo->setObjectName(QStringLiteral("AnalysisFormCombo"));
			combo->addItem(formValues[row]);
			grid->addWidget(combo, row, 1);
		}
	}

	const QStringList statLabels = { QStringLiteral("Peak"), QStringLiteral("Latency"),
		QStringLiteral("Init"), QStringLiteral("CPU") };
	const QStringList statValues = { QStringLiteral("-6.0 dB"), QStringLiteral("0.0 ms (0 s.)"),
		QStringLiteral("0.4 ms"), QStringLiteral("0.1 %") };
	for (int i = 0; i < statLabels.size(); i++)
	{
		QFrame* chipFrame = new QFrame;
		chipFrame->setObjectName(QStringLiteral("AnalysisStatChip"));
		chipFrame->setAttribute(Qt::WA_StyledBackground, true);
		QHBoxLayout* chipLayout = new QHBoxLayout(chipFrame);
		chipLayout->setContentsMargins(10, 3, 10, 3);
		chipLayout->setSpacing(6);
		QLabel* label = new QLabel(statLabels[i]);
		label->setObjectName(QStringLiteral("AnalysisStatLabel"));
		chipLayout->addWidget(label);
		QLabel* value = new QLabel(statValues[i]);
		value->setObjectName(QStringLiteral("AnalysisStatValue"));
		value->setProperty("severity", QStringLiteral("normal"));
		chipLayout->addWidget(value);
		grid->addWidget(chipFrame, 4 + i, 0, 1, 2);
	}
	grid->setRowStretch(8, 1);

	EqGraphView* graph = new EqGraphView(panel);
	graph->setObjectName(QStringLiteral("ModernAnalysisGraph"));
	dockLayout->addWidget(bar);
	dockLayout->addWidget(graph, 1);
	return panel;
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

// X2 gate: a row must fit its 960px viewport in every skin. A visible
// horizontal scrollbar inside the row is the overflow defect the adversarial
// review flagged on soft/minimal; failing the render makes CI keep the
// broken shot as evidence instead of shipping it silently.
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
QList<FilterCardRow*> buildRows(QScrollArea& scrollArea, const QString& configPath, const QList<QString>& lines)
{
	// Mirror MainWindow's hosting: widgetResizable makes the scroll area drive
	// the table's width, which FilterCardRow::sizeHint reads back through
	// getPreferredWidth(). Without it the table never gets a real size and
	// every row collapses to a few pixels.
	scrollArea.setWidgetResizable(true);
	FilterTable* table = new FilterTable(nullptr);
	if (qEnvironmentVariableIsSet("EAPO_GALLERY_LEGACY"))
		table->setRenderMode(FilterTable::LegacyRows);
	scrollArea.setWidget(table);
	QList<std::shared_ptr<AbstractAPOInfo>> outputDevices, inputDevices;
	galleryDevices(outputDevices, inputDevices);
	// The card path renders deviceless on purpose (its editors derive their
	// ports from the command text). The heritage dump selects a synthetic
	// device instead: the legacy CopyFilterGUI scene only populates through
	// configureChannels(), which is empty without one.
	if (qEnvironmentVariableIsSet("EAPO_GALLERY_LEGACY") && !outputDevices.isEmpty())
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
	GUIHelper::applySkinPalette();

	const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
	int failures = 0;
	failures += renderStates(outDir, skinId, mode, configPath, galleryRows(), false);
	failures += renderStates(outDir, skinId, mode, configPath, galleryRows(), true);

	// The skin's "add filter" picker with the real template set, captured the
	// same way the rows are. A throwaway FilterTable supplies the entries; its
	// factories are the same ones chooseFilterTemplate consults at runtime.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, { QStringLiteral("Preamp: -6 dB") });
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		FilterPickerView* picker = SkinManager::instance()->createFilterPicker(nullptr);
		picker->setEntries(table != nullptr ? table->filterPickerEntries() : QList<FilterPickerEntry>());
		picker->adjustSize();
		picker->show();
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("normal")) ? 0 : 1;
		// X6 showcase states. Pickers that have not implemented a state render
		// their normal look (base no-op), so the shot count stays fixed.
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::HoverFirstEntry);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("hover")) ? 0 : 1;
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::EmptySearch);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("empty")) ? 0 : 1;
		delete picker;
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

	// The analysis dock's settings cell beside the graph (feedback round):
	// the one piece of shared chrome the strip redesign moved, judged per
	// skin like the toolbar.
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
	const int perSkinMode = static_cast<int>(galleryRows().size()) * kStatesPerRow + kExtraShotsPerSkinMode;
	const int expected = static_cast<int>(skinIds.size()) * 2 * perSkinMode;
	const int actual = static_cast<int>(outDir.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files).size());
	if (actual != expected)
	{
		qWarning("SkinGallery: expected %d shots (%d skins x 2 modes x (%d rows x %d + %d extras)), wrote %d",
			expected, static_cast<int>(skinIds.size()), static_cast<int>(galleryRows().size()), kStatesPerRow, kExtraShotsPerSkinMode, actual);
		failures++;
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
