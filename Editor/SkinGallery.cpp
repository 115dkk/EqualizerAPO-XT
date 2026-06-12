#include "SkinGallery.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QScrollArea>
#include <QString>
#include <QToolBar>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/TitleBar.h"

namespace
{
struct GalleryRow
{
	QString name;
	QString line;
};

// Representative rows: a parametric filter, a shelf filter (three knobs in
// the legacy BiQuad GUI hosted by the card body), an Include row and a VST
// row. The VST library is intentionally unresolvable; the card renders its
// not-loaded state, which is the chrome the gallery is after.
QList<GalleryRow> galleryRows()
{
	return {
		{ QStringLiteral("filter"), QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71") },
		{ QStringLiteral("shelf"), QStringLiteral("Filter 2: ON HSC Fc 8000 Hz Gain -2.5 dB Q 0.71") },
		{ QStringLiteral("include"), QStringLiteral("Include: example.txt") },
		{ QStringLiteral("vst"), QStringLiteral("VSTPlugin: Library example.dll") }
	};
}

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
QList<FilterCardRow*> buildRows(QScrollArea& scrollArea, const QList<QString>& lines)
{
	// Mirror MainWindow's hosting: widgetResizable makes the scroll area drive
	// the table's width, which FilterCardRow::sizeHint reads back through
	// getPreferredWidth(). Without it the table never gets a real size and
	// every row collapses to a few pixels.
	scrollArea.setWidgetResizable(true);
	FilterTable* table = new FilterTable(nullptr);
	scrollArea.setWidget(table);
	table->updateDeviceAndChannelMask(nullptr, 0);
	table->initialize(&scrollArea, {}, {});
	// The config path is synthetic: it only namespaces per-file row prefs in
	// the registry, and this name can never collide with a real file.
	table->setLines(QStringLiteral(":skin-gallery:"), lines);
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
	const QList<GalleryRow>& rows, bool commented)
{
	QList<QString> lines;
	for (const GalleryRow& row : rows)
		lines.append(commented ? QStringLiteral("# ") + row.line : row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	QList<FilterCardRow*> rowWidgets = buildRows(scrollArea, lines);
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
			failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("disabled")) ? 0 : 1;
			continue;
		}

		failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("normal")) ? 0 : 1;
		setHoverEquivalent(row, true);
		failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("hover")) ? 0 : 1;
		setHoverEquivalent(row, false);
	}
	return failures;
}

int renderSkin(const QDir& outDir, const QString& skinId, bool dark)
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
	failures += renderStates(outDir, skinId, mode, galleryRows(), false);
	failures += renderStates(outDir, skinId, mode, galleryRows(), true);

	// The skin's "add filter" picker with the real template set, captured the
	// same way the rows are. A throwaway FilterTable supplies the entries; its
	// factories are the same ones chooseFilterTemplate consults at runtime.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, { QStringLiteral("Preamp: -6 dB") });
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		FilterPickerView* picker = SkinManager::instance()->createFilterPicker(nullptr);
		picker->setEntries(table != nullptr ? table->filterPickerEntries() : QList<FilterPickerEntry>());
		picker->adjustSize();
		picker->show();
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("normal")) ? 0 : 1;
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
	return failures;
}
}

namespace SkinGallery
{
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

	int failures = 0;
	for (const QString& skinId : skinIds)
	{
		failures += renderSkin(outDir, skinId.trimmed(), true);
		failures += renderSkin(outDir, skinId.trimmed(), false);
	}

	return failures == 0 ? 0 : 1;
}
}
