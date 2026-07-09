#include "GraphicEQCardEditor.h"

#include <cfloat>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/guis/GraphicEQFilterGUIScene.h"
#include "Editor/helpers/GUIHelper.h"
#include "FilterCardEditorRegistry.h"
#include "filters/GraphicEQCommand.h"

using std::sort;
using std::vector;

QRegularExpression GraphicEQCardEditor::numberRegEx("[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?");

namespace
{
// The dB span the card frames by default (the legacy GUI showed whatever
// scroll region the fixed 1000x500 scene happened to land on - usually the
// +60..+100 dB corner, which read as an empty grid).
const double defaultVisibleDbSpan = 42.0;

IFilterGUI* createGraphicEQCardEditor(FilterTable* filterTable, const QString& command, const QString& parameters)
{
	// Exact-case contract, matching the legacy GUI factory and the engine
	// parser: a "graphiceq:" line is not a command the engine runs, so it
	// falls through to the raw-text card instead of getting a live editor.
	if (command != QStringLiteral("GraphicEQ"))
		return nullptr;

	GraphicEQCommand cmd;
	cmd.parse(parameters.toStdWString());
	return new GraphicEQCardEditor(cmd.nodes, filterTable != nullptr ? filterTable->getConfigPath() : QString(), filterTable);
}
}

REGISTER_FILTER_CARD_EDITOR(graphiceq, createGraphicEQCardEditor)

GraphicEQCardPlotView::GraphicEQCardPlotView(QWidget* parent)
	: GraphicEQFilterGUIView(parent)
{
	setMouseTracking(true);
	setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
	setDragMode(QGraphicsView::RubberBandDrag);
	setFrameShape(QFrame::NoFrame);
	// The horizontal axis always fits the viewport (fitWidth), so a
	// horizontal scrollbar would only ever flag a bug - and the 960px skin
	// gallery gate treats one as a failure.
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

void GraphicEQCardPlotView::setVerticalFrame(double topDb)
{
	frameTopDb = topDb;
	applyFrame();
}

double GraphicEQCardPlotView::verticalFrameTopDb() const
{
	return frameTopDb;
}

void GraphicEQCardPlotView::resizeEvent(QResizeEvent* event)
{
	GraphicEQFilterGUIView::resizeEvent(event);
	fitWidth();
}

void GraphicEQCardPlotView::wheelEvent(QWheelEvent* event)
{
	// The dB axis is the only zoomable one on the card; the frequency axis is
	// pinned to the audible window.
	event->accept();
	zoom(0, event->angleDelta().y(), event->position().x(), event->position().y());
}

void GraphicEQCardPlotView::scrollContentsBy(int dx, int dy)
{
	GraphicEQFilterGUIView::scrollContentsBy(dx, dy);
	// Track where the user put the vertical frame (right-drag pan, wheel
	// zoom anchor shifts) so the next width refit restores their view, not
	// the construction-time default.
	if (!applyingFrame && scene() != nullptr)
		frameTopDb = scene()->yToDb(mapToScene(0, 0).y());
}

void GraphicEQCardPlotView::applyFrame()
{
	FrequencyPlotScene* s = scene();
	if (s == nullptr)
		return;
	applyingFrame = true;
	// setZoom rebuilt the scene rect, which resets the view onto the scene's
	// top-left (1 Hz, +100 dB) corner; pin the window back to 20 Hz and the
	// remembered top dB line.
	horizontalScrollBar()->setValue(int(round(s->hzToX(20.0))));
	const double y = s->dbToY(frameTopDb);
	if (y >= 0)
		verticalScrollBar()->setValue(int(round(y)));
	applyingFrame = false;
}

void GraphicEQCardPlotView::fitWidth()
{
	FrequencyPlotScene* s = scene();
	if (s == nullptr)
		return;
	const double viewportWidth = viewport()->width();
	if (viewportWidth <= 0)
		return;
	// Fit the 20 Hz - 20 kHz window (the range the band layouts and the
	// legacy default scroll actually use) instead of the full 1 Hz - 30 kHz
	// scene, whose sub-audible left margin only wastes card width.
	const double windowSpan = (s->hzToX(20000.0) - s->hzToX(20.0)) / s->getZoomX();
	if (windowSpan <= 0)
		return;
	const double newZoomX = qMax(0.05, viewportWidth / windowSpan);
	if (qAbs(newZoomX - s->getZoomX()) > 1e-9)
		s->setZoom(newZoomX, s->getZoomY());
	applyFrame();
	resetCachedContent();
	viewport()->update();
	updateHRuler();
}

GraphicEQCardEditor::GraphicEQCardEditor(const vector<FilterNode>& nodes, const QString& configPath, FilterTable* filterTable, QWidget* parent)
	: IFilterGUI(parent), configPath(configPath)
{
	Q_UNUSED(filterTable);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(8);

	QHBoxLayout* controlsLayout = new QHBoxLayout();
	controlsLayout->setContentsMargins(0, 0, 0, 0);
	controlsLayout->setSpacing(6);

	modeCombo = new QComboBox(this);
	modeCombo->setObjectName(QStringLiteral("GraphicEQModeCombo"));
	// X5 selector grammar: paramSelector marks a real mode selector, so each
	// skin dresses it with its established parameter-selector treatment.
	modeCombo->setProperty("paramSelector", true);
	modeCombo->addItem(tr("15-band"));
	modeCombo->addItem(tr("31-band"));
	modeCombo->addItem(tr("variable"));
	modeCombo->setToolTip(tr("Band layout"));
	connect(modeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(modeSelected(int)));
	controlsLayout->addWidget(modeCombo);

	controlsLayout->addStretch(1);

	const struct
	{
		QToolButton** member = nullptr;
		const char* icon = nullptr;
		QString toolTip;
		const char* slot = nullptr;
	} actions[] = {
		{ &importButton, ":/icons/modern/folder-open.svg", tr("Import"), SLOT(importTriggered()) },
		{ &exportButton, ":/icons/modern/save.svg", tr("Export"), SLOT(exportTriggered()) },
		{ &invertButton, ":/icons/invert_response.svg", tr("Invert response"), SLOT(invertTriggered()) },
		{ &normalizeButton, ":/icons/normalize_response.svg", tr("Normalize response"), SLOT(normalizeTriggered()) },
		{ &resetButton, ":/icons/reset_response.svg", tr("Reset response"), SLOT(resetTriggered()) }
	};
	for (const auto& action : actions)
	{
		QToolButton* button = new QToolButton(this);
		button->setObjectName(QStringLiteral("GraphicEQActionButton"));
		button->setAutoRaise(true);
		button->setToolTip(action.toolTip);
		button->setIconSize(GUIHelper::scale(QSize(16, 16)));
		button->setProperty("modernIcon", QString::fromLatin1(action.icon));
		connect(button, SIGNAL(clicked()), this, action.slot);
		controlsLayout->addWidget(button);
		*action.member = button;
	}

	mainLayout->addLayout(controlsLayout);

	QHBoxLayout* plotLayout = new QHBoxLayout();
	plotLayout->setContentsMargins(0, 0, 0, 0);
	plotLayout->setSpacing(8);

	plotView = new GraphicEQCardPlotView(this);
	plotView->setObjectName(QStringLiteral("GraphicEQCardPlot"));
	plotView->setFixedHeight(GUIHelper::scale(200));
	plotView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	plotLayout->addWidget(plotView, 1);

	bandTable = new QTableWidget(this);
	bandTable->setObjectName(QStringLiteral("GraphicEQBandTable"));
	bandTable->setColumnCount(2);
	bandTable->setHorizontalHeaderLabels({ tr("Freq."), tr("Gain") });
	bandTable->horizontalHeader()->setVisible(false);
	bandTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	bandTable->horizontalHeader()->setMinimumSectionSize(GUIHelper::scale(10));
	bandTable->verticalHeader()->setMinimumSectionSize(GUIHelper::scale(23));
	bandTable->verticalHeader()->setDefaultSectionSize(GUIHelper::scale(23));
	bandTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	bandTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	bandTable->setWordWrap(false);
	bandTable->setFixedWidth(GUIHelper::scale(150));
	bandTable->setFixedHeight(GUIHelper::scale(200));
	connect(bandTable, SIGNAL(cellChanged(int,int)), this, SLOT(tableCellChanged(int,int)));
	connect(bandTable, SIGNAL(itemSelectionChanged()), this, SLOT(tableSelectionChanged()));
	plotLayout->addWidget(bandTable);

	mainLayout->addLayout(plotLayout);

	scene = new GraphicEQFilterGUIScene(plotView);
	plotView->setScene(scene);

	connect(scene, SIGNAL(nodeInserted(int,double,double)), this, SLOT(insertRow(int,double,double)));
	connect(scene, &GraphicEQFilterGUIScene::nodeRemoved, this, &GraphicEQCardEditor::removeRow);
	connect(scene, SIGNAL(nodeUpdated(int,double,double)), this, SLOT(updateRow(int,double,double)));
	connect(scene, SIGNAL(nodeMoved(int,int)), this, SLOT(moveRow(int,int)));
	connect(scene, SIGNAL(nodeSelectionChanged(int,bool)), this, SLOT(selectRow(int,bool)));
	connect(scene, SIGNAL(updateModel()), this, SIGNAL(updateModel()));

	scene->setNodes(nodes);
	syncModeCombo(scene->verifyBands(nodes));

	// Default framing: with the width pinned, frame the dB axis around the
	// response instead of the +60..+100 dB corner the legacy scene woke up
	// in. loadPreferences overrides this when the row was framed before.
	double minGain = 0.0;
	double maxGain = 0.0;
	for (const FilterNode& node : nodes)
	{
		minGain = qMin(minGain, node.dbGain);
		maxGain = qMax(maxGain, node.dbGain);
	}
	const int viewportHeight = qMax(1, GUIHelper::scale(200) - GUIHelper::scale(20));
	// visible span = viewport / (500 * zoomY) * 200 dB; solve for zoomY.
	const double span = qMax(defaultVisibleDbSpan, maxGain - minGain + 12.0);
	const double zoomY = viewportHeight * 200.0 / (500.0 * span);
	scene->setZoom(scene->getZoomX(), zoomY);
	plotView->setVerticalFrame((minGain + maxGain) / 2.0 + span / 2.0);

	applySkinToPlot();
	retintActions();
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		applySkinToPlot();
		retintActions();
	});
}

void GraphicEQCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("GraphicEQ");
	GraphicEQCommand cmd;
	cmd.nodes = scene->getNodes();
	parameters += QString::fromStdWString(cmd.serialize());
}

void GraphicEQCardEditor::loadPreferences(const QVariantMap& prefs)
{
	bool ok = false;
	const double zoomY = prefs.value(QStringLiteral("cardZoomY")).toDouble(&ok);
	if (ok && zoomY > 0)
		scene->setZoom(scene->getZoomX(), zoomY);
	// The frame is stored in dB (device- and zoom-independent), not as a
	// scrollbar offset like the legacy GUI's prefs.
	const double topDb = prefs.value(QStringLiteral("cardFrameTopDb")).toDouble(&ok);
	if (ok)
		plotView->setVerticalFrame(topDb);
}

void GraphicEQCardEditor::storePreferences(QVariantMap& prefs)
{
	prefs.insert(QStringLiteral("cardZoomY"), scene->getZoomY());
	prefs.insert(QStringLiteral("cardFrameTopDb"), plotView->verticalFrameTopDb());
}

void GraphicEQCardEditor::insertRow(int index, double hz, double db)
{
	bandTable->blockSignals(true);
	bandTable->insertRow(index);
	bandTable->setItem(index, 0, new QTableWidgetItem(QStringLiteral("%0").arg(hz)));
	bandTable->setItem(index, 1, new QTableWidgetItem(QStringLiteral("%0").arg(db)));
	// New rows must follow the active mode's frequency-lock rule.
	if (scene->getBandCount() != -1)
	{
		QTableWidgetItem* freqItem = bandTable->item(index, 0);
		freqItem->setFlags(freqItem->flags() & ~(Qt::ItemIsEditable | Qt::ItemIsEnabled));
	}
	bandTable->blockSignals(false);
}

void GraphicEQCardEditor::removeRow(int index)
{
	bandTable->blockSignals(true);
	bandTable->removeRow(index);
	bandTable->blockSignals(false);
}

void GraphicEQCardEditor::updateRow(int index, double hz, double db)
{
	bandTable->blockSignals(true);
	bandTable->item(index, 0)->setText(QStringLiteral("%0").arg(hz));
	bandTable->item(index, 1)->setText(QStringLiteral("%0").arg(db));
	bandTable->blockSignals(false);
}

void GraphicEQCardEditor::moveRow(int fromIndex, int toIndex)
{
	bandTable->blockSignals(true);
	QTableWidgetItem* freqItem = bandTable->takeItem(fromIndex, 0);
	QTableWidgetItem* gainItem = bandTable->takeItem(fromIndex, 1);
	const bool selected = bandTable->selectionModel()->isRowSelected(fromIndex, bandTable->rootIndex());
	bandTable->removeRow(fromIndex);
	bandTable->insertRow(toIndex);
	bandTable->setItem(toIndex, 0, freqItem);
	bandTable->setItem(toIndex, 1, gainItem);
	if (selected)
	{
		QModelIndex modelIndex = bandTable->model()->index(toIndex, 0);
		bandTable->selectionModel()->select(modelIndex, QItemSelectionModel::Rows | QItemSelectionModel::Select);
		bandTable->scrollTo(modelIndex);
	}
	bandTable->blockSignals(false);
}

void GraphicEQCardEditor::selectRow(int index, bool select)
{
	bandTable->blockSignals(true);
	QItemSelectionModel::SelectionFlags command = QItemSelectionModel::Rows;
	command |= select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect;
	QModelIndex modelIndex = bandTable->model()->index(index, 0);
	bandTable->selectionModel()->select(modelIndex, command);
	bandTable->scrollTo(modelIndex);
	bandTable->blockSignals(false);
}

void GraphicEQCardEditor::tableCellChanged(int row, int column)
{
	if (column == 0)
	{
		QTableWidgetItem* freqItem = bandTable->item(row, 0);
		bool ok;
		double freq = freqItem->text().toDouble(&ok);
		if (ok)
		{
			scene->setNode(row, freq, scene->getNodes()[row].dbGain);
		}
		else
		{
			freq = scene->getNodes()[row].freq;
			freqItem->setText(QStringLiteral("%0").arg(freq));
		}
	}
	else if (column == 1)
	{
		QTableWidgetItem* gainItem = bandTable->item(row, 1);
		bool ok;
		double gain = gainItem->text().toDouble(&ok);
		if (ok)
		{
			scene->setNode(row, scene->getNodes()[row].freq, gain);
		}
		else
		{
			gain = scene->getNodes()[row].dbGain;
			gainItem->setText(QStringLiteral("%0").arg(gain));
		}
	}
}

void GraphicEQCardEditor::tableSelectionChanged()
{
	QSet<int> selectedRows;
	for (QTableWidgetItem* item : bandTable->selectedItems())
		selectedRows.insert(item->row());
	scene->setSelectedNodes(selectedRows);
}

void GraphicEQCardEditor::modeSelected(int comboIndex)
{
	applyBandCount(comboIndex == 0 ? 15 : (comboIndex == 1 ? 31 : -1));
}

void GraphicEQCardEditor::applyBandCount(int bandCount)
{
	scene->setBandCount(bandCount);
	setFreqEditable(bandCount == -1);
}

void GraphicEQCardEditor::syncModeCombo(int bandCount)
{
	const int comboIndex = bandCount == 15 ? 0 : (bandCount == 31 ? 1 : 2);
	modeCombo->blockSignals(true);
	modeCombo->setCurrentIndex(comboIndex);
	modeCombo->blockSignals(false);
	applyBandCount(bandCount);
}

void GraphicEQCardEditor::setFreqEditable(bool editable)
{
	bandTable->blockSignals(true);
	for (int i = 0; i < bandTable->rowCount(); i++)
	{
		QTableWidgetItem* item = bandTable->item(i, 0);
		if (editable)
			item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled);
		else
			item->setFlags(item->flags() & ~(Qt::ItemIsEditable | Qt::ItemIsEnabled));
	}
	bandTable->blockSignals(false);
}

void GraphicEQCardEditor::applySkinToPlot()
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	FrequencyPlotColors colors;
	colors.tokenDriven = true;
	colors.grid = QColor(tokens.graphGridMajor);
	colors.curve = QColor(tokens.accent);
	colors.node = QColor(tokens.surfaceRaised);
	colors.nodeInk = QColor(tokens.text);
	colors.nodeSelected = QColor(tokens.accent);
	// Readable numeral on the accent disc in both modes: pick by luminance.
	const QColor accent(tokens.accent);
	colors.nodeSelectedInk = accent.lightness() < 140 ? QColor(Qt::white) : QColor(Qt::black);
	colors.rulerInk = QColor(tokens.mutedText);
	colors.rulerCursorInk = QColor(tokens.accent);
	colors.rulerCursorBox = QColor(tokens.graph);
	colors.rulerCursorFrame = QColor(tokens.border);
	scene->setPlotColors(colors);
	plotView->setBackgroundBrush(QColor(tokens.graph));
	plotView->resetCachedContent();
	plotView->viewport()->update();
}

void GraphicEQCardEditor::retintActions()
{
	const QColor ink(SkinManager::instance()->tokens().text);
	for (QToolButton* button : { importButton, exportButton, invertButton, normalizeButton, resetButton })
	{
		if (button == nullptr)
			continue;
		button->setIcon(GUIHelper::tintedIcon(button->property("modernIcon").toString(), ink, 16));
	}
}

void GraphicEQCardEditor::importTriggered()
{
	QFileInfo fileInfo(configPath);
	QFileDialog dialog(this, tr("Import frequency response"), fileInfo.absolutePath(), "*.csv");
	dialog.setFileMode(QFileDialog::ExistingFiles);
	QStringList nameFilters;
	nameFilters.append(tr("Frequency response (*.csv)"));
	nameFilters.append(tr("All files (*.*)"));
	dialog.setNameFilters(nameFilters);
	if (dialog.exec() != QDialog::Accepted)
		return;

	vector<FilterNode> newNodes;
	for (const QString& path : dialog.selectedFiles())
	{
		QFile file(path);
		if (file.open(QFile::ReadOnly))
		{
			QTextStream stream(&file);
			while (!stream.atEnd())
			{
				QString text = stream.readLine();
				if (text.startsWith('*'))
					continue;

				if (!text.contains('.'))
					text = text.replace(',', '.');
				QRegularExpressionMatchIterator it = numberRegEx.globalMatch(text);
				while (it.hasNext())
				{
					QRegularExpressionMatch match = it.next();
					bool ok;
					double freq = match.captured().toDouble(&ok);
					if (ok && it.hasNext())
					{
						match = it.next();
						double gain = match.captured().toDouble(&ok);
						if (ok)
							newNodes.push_back(FilterNode(freq, gain));
					}
				}
			}
		}
	}
	sort(newNodes.begin(), newNodes.end());

	scene->setNodes(newNodes);

	// A band-layout switch persists through setBandCount's own updateModel;
	// only the unchanged-layout import needs the explicit nudge.
	const int bandCount = scene->verifyBands(newNodes);
	if (bandCount != scene->getBandCount())
		syncModeCombo(bandCount);
	else
		emit updateModel();
}

void GraphicEQCardEditor::exportTriggered()
{
	QFileInfo fileInfo(configPath);
	QFileDialog dialog(this, tr("Export frequency response"), fileInfo.absolutePath(), "*.csv");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	QStringList nameFilters;
	nameFilters.append(tr("Frequency response (*.csv)"));
	nameFilters.append(tr("All files (*.*)"));
	dialog.setNameFilters(nameFilters);
	dialog.setDefaultSuffix(".csv");
	if (dialog.exec() != QDialog::Accepted)
		return;

	QFile file(dialog.selectedFiles().first());
	if (file.open(QFile::WriteOnly | QFile::Truncate))
	{
		QTextStream stream(&file);
		for (const FilterNode& node : scene->getNodes())
			stream << node.freq << '\t' << node.dbGain << '\n';
		stream.flush();
	}
}

void GraphicEQCardEditor::invertTriggered()
{
	vector<FilterNode> newNodes = scene->getNodes();
	for (FilterNode& node : newNodes)
		node.dbGain = -node.dbGain;

	scene->setNodes(newNodes);
	emit updateModel();
}

void GraphicEQCardEditor::normalizeTriggered()
{
	vector<FilterNode> newNodes = scene->getNodes();

	double maxGain = -DBL_MAX;
	for (const FilterNode& node : newNodes)
		maxGain = qMax(maxGain, node.dbGain);

	if (maxGain != 0 && maxGain != -DBL_MAX)
	{
		for (FilterNode& node : newNodes)
			node.dbGain -= maxGain;

		scene->setNodes(newNodes);
		emit updateModel();
	}
}

void GraphicEQCardEditor::resetTriggered()
{
	vector<FilterNode> newNodes = scene->getNodes();
	QSet<int> selectedIndices = scene->getSelectedIndices();

	int index = 0;
	for (FilterNode& node : newNodes)
	{
		if (selectedIndices.isEmpty() || selectedIndices.contains(index))
			node.dbGain = 0.0;
		index++;
	}

	scene->setNodes(newNodes);
	scene->setSelectedNodes(selectedIndices);
	emit updateModel();
}
