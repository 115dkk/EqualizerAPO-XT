#include "GraphicEQCardEditor.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/FrequencyPlotScene.h"
#include "Editor/widgets/GraphicEQPlotWidget.h"
#include "Editor/widgets/ValueScrubBox.h"
#include "FilterCardEditorRegistry.h"
#include "filters/GraphicEQCommand.h"

using std::sort;
using std::vector;

QRegularExpression GraphicEQCardEditor::numberRegEx("[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?");

namespace
{
IFilterGUI* createGraphicEQCardEditor(FilterTable* filterTable, const QString& command, const QString& parameters)
{
	// Exact-key contract, matching the legacy GUI factory and the engine parser.
	// FilterCardModel::canonicalCommand already rejects both the wrong casing
	// and the trailing-token spelling ("GraphicEQ 2:" runs nowhere), so this is
	// the second lock on a door that is already shut - kept because the card is
	// one keystroke away from writing a GraphicEQ line over whatever it opened.
	if (command != QStringLiteral("GraphicEQ"))
		return nullptr;

	GraphicEQCommand cmd;
	cmd.parse(parameters.toStdWString());
	return new GraphicEQCardEditor(cmd.nodes, filterTable != nullptr ? filterTable->getConfigPath() : QString(), filterTable);
}
}

REGISTER_FILTER_CARD_EDITOR(GraphicEQ, createGraphicEQCardEditor)

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
	// paramSelector marks a real mode selector, so each skin dresses it with
	// its established parameter-selector treatment.
	modeCombo->setProperty("paramSelector", true);
	modeCombo->addItem(tr("15-band"));
	modeCombo->addItem(tr("31-band"));
	modeCombo->addItem(tr("variable"));
	modeCombo->setToolTip(tr("Band layout"));
	connect(modeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(modeSelected(int)));
	controlsLayout->addWidget(modeCombo);

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

	// The buttons follow the mode selector instead of riding the card's
	// right edge; the stretch owns the leftover width.
	controlsLayout->addStretch(1);

	mainLayout->addLayout(controlsLayout);

	plot = new GraphicEQPlotWidget(this);
	plot->setFixedHeight(GUIHelper::scale(210));
	connect(plot, &GraphicEQPlotWidget::nodesEdited, this, [this]() {
		syncReadout();
		emit updateModel();
	});
	connect(plot, &GraphicEQPlotWidget::focusedNodeChanged, this, &GraphicEQCardEditor::focusedNodeChanged);
	mainLayout->addWidget(plot);

	// Selected-band readout strip: the precise entry surface that replaces
	// the legacy side table. Freq/Gain ride the value-scrub grammar every
	// skin already dresses; the caption is a quiet secondary-ink label.
	QWidget* readout = new QWidget(this);
	readout->setObjectName(QStringLiteral("GraphicEQReadout"));
	QHBoxLayout* readoutLayout = new QHBoxLayout(readout);
	readoutLayout->setContentsMargins(0, 0, 0, 0);
	readoutLayout->setSpacing(8);

	bandCaption = new QLabel(readout);
	bandCaption->setObjectName(QStringLiteral("GraphicEQBandCaption"));
	readoutLayout->addWidget(bandCaption);
	readoutLayout->addStretch(1);

	QLabel* freqLabel = new QLabel(tr("Freq."), readout);
	freqLabel->setObjectName(QStringLiteral("GraphicEQReadoutLabel"));
	readoutLayout->addWidget(freqLabel);
	freqBox = new ValueScrubBox(readout);
	freqBox->setObjectName(QStringLiteral("GraphicEQFreqBox"));
	freqBox->setDecimals(1);
	freqBox->setRange(20.0, 20000.0);
	freqBox->setSuffix(QStringLiteral(" Hz"));
	freqBox->setKeyboardTracking(false);
	connect(freqBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GraphicEQCardEditor::readoutFreqChanged);
	readoutLayout->addWidget(freqBox);

	QLabel* gainLabel = new QLabel(tr("Gain"), readout);
	gainLabel->setObjectName(QStringLiteral("GraphicEQReadoutLabel"));
	readoutLayout->addWidget(gainLabel);
	gainBox = new ValueScrubBox(readout);
	gainBox->setObjectName(QStringLiteral("GraphicEQGainBox"));
	gainBox->setDecimals(1);
	gainBox->setRange(-100.0, 100.0);
	gainBox->setSingleStep(0.5);
	gainBox->setSuffix(QStringLiteral(" dB"));
	gainBox->setKeyboardTracking(false);
	connect(gainBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GraphicEQCardEditor::readoutGainChanged);
	readoutLayout->addWidget(gainBox);

	mainLayout->addWidget(readout);

	plot->setNodes(nodes);
	syncModeCombo(verifyBands(nodes));
	plot->frameToResponse();
	syncReadout();

	retintActions();
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		retintActions();
	});
}

void GraphicEQCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("GraphicEQ");
	GraphicEQCommand cmd;
	cmd.nodes = plot->nodes();
	parameters += QString::fromStdWString(cmd.serialize());
}

void GraphicEQCardEditor::loadPreferences(const QVariantMap& prefs)
{
	// The frame is stored in dB (device- and layout-independent).
	bool okTop = false;
	bool okSpan = false;
	const double storedTop = prefs.value(QStringLiteral("cardDbTop")).toDouble(&okTop);
	const double storedSpan = prefs.value(QStringLiteral("cardDbSpan")).toDouble(&okSpan);
	if (okTop && okSpan)
		plot->setFrame(storedTop, storedSpan);
}

void GraphicEQCardEditor::storePreferences(QVariantMap& prefs)
{
	prefs.insert(QStringLiteral("cardDbTop"), plot->frameTopDb());
	prefs.insert(QStringLiteral("cardDbSpan"), plot->frameSpanDb());
}

void GraphicEQCardEditor::modeSelected(int comboIndex)
{
	const int bandCount = comboIndex == 0 ? 15 : (comboIndex == 1 ? 31 : -1);
	applyBandCount(bandCount);
}

void GraphicEQCardEditor::applyBandCount(int bandCount)
{
	if (bandCount == plot->bandCount())
		return;

	if (bandCount != -1)
	{
		// Resample the current response onto the fixed band layout through
		// the shared engine-side interpolator (the legacy scene's rule).
		const vector<double>& bands = FrequencyPlotScene::getBands(bandCount);
		vector<FilterNode> resampled;
		vector<FilterNode> current = plot->nodes();
		GainCurveIterator gainIterator(current);
		resampled.reserve(bands.size());
		for (double band : bands)
			resampled.push_back(FilterNode(band, std::round(gainIterator.gainAt(band) * 100.0) / 100.0));
		plot->setBandCount(bandCount);
		plot->setNodes(resampled);
		syncReadout();
		emit updateModel();
	}
	else
	{
		plot->setBandCount(bandCount);
		syncReadout();
	}

	freqBox->setEnabled(bandCount == -1 && plot->focusedNode() >= 0);
}

void GraphicEQCardEditor::syncModeCombo(int bandCount)
{
	const int comboIndex = bandCount == 15 ? 0 : (bandCount == 31 ? 1 : 2);
	modeCombo->blockSignals(true);
	modeCombo->setCurrentIndex(comboIndex);
	modeCombo->blockSignals(false);
	plot->setBandCount(bandCount);
	freqBox->setEnabled(bandCount == -1 && plot->focusedNode() >= 0);
}

void GraphicEQCardEditor::focusedNodeChanged(int)
{
	syncReadout();
}

void GraphicEQCardEditor::syncReadout()
{
	syncingReadout = true;
	const int index = plot->focusedNode();
	const vector<FilterNode>& nodes = plot->nodes();
	const bool valid = index >= 0 && index < int(nodes.size());
	if (valid)
	{
		bandCaption->setText(tr("Band %0 / %1").arg(index + 1).arg(nodes.size()));
		freqBox->setValue(nodes[size_t(index)].freq);
		gainBox->setValue(nodes[size_t(index)].dbGain);
	}
	else
	{
		bandCaption->setText(tr("No bands"));
		freqBox->setValue(1000.0);
		gainBox->setValue(0.0);
	}
	freqBox->setEnabled(valid && plot->bandCount() == -1);
	gainBox->setEnabled(valid);
	syncingReadout = false;
}

void GraphicEQCardEditor::readoutFreqChanged(double value)
{
	if (syncingReadout || plot->focusedNode() < 0)
		return;
	const vector<FilterNode>& nodes = plot->nodes();
	plot->setNodeValues(plot->focusedNode(), value, nodes[size_t(plot->focusedNode())].dbGain);
}

void GraphicEQCardEditor::readoutGainChanged(double value)
{
	if (syncingReadout || plot->focusedNode() < 0)
		return;
	const vector<FilterNode>& nodes = plot->nodes();
	plot->setNodeValues(plot->focusedNode(), nodes[size_t(plot->focusedNode())].freq, value);
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

int GraphicEQCardEditor::verifyBands(const vector<FilterNode>& nodes)
{
	const vector<double>& bands = FrequencyPlotScene::getBands(int(nodes.size()));
	if (bands.empty())
		return -1;
	for (size_t i = 0; i < nodes.size(); i++)
	{
		if (std::abs(nodes[i].freq - bands[i]) > 0.1)
			return -1;
	}
	return int(nodes.size());
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
	GUIHelper::prepareFileDialog(dialog);
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

	const int bandCount = verifyBands(newNodes);
	plot->setBandCount(bandCount);
	plot->setNodes(newNodes);
	syncModeCombo(bandCount);
	plot->frameToResponse();
	syncReadout();
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
	GUIHelper::prepareFileDialog(dialog);
	if (dialog.exec() != QDialog::Accepted)
		return;

	QFile file(dialog.selectedFiles().first());
	if (file.open(QFile::WriteOnly | QFile::Truncate))
	{
		QTextStream stream(&file);
		for (const FilterNode& node : plot->nodes())
			stream << node.freq << '\t' << node.dbGain << '\n';
		stream.flush();
	}
}

void GraphicEQCardEditor::invertTriggered()
{
	vector<FilterNode> newNodes = plot->nodes();
	for (FilterNode& node : newNodes)
		node.dbGain = -node.dbGain;

	plot->setNodes(newNodes);
	syncReadout();
	emit updateModel();
}

void GraphicEQCardEditor::normalizeTriggered()
{
	vector<FilterNode> newNodes = plot->nodes();

	double maxGain = -DBL_MAX;
	for (const FilterNode& node : newNodes)
		maxGain = qMax(maxGain, node.dbGain);

	if (maxGain != 0 && maxGain != -DBL_MAX)
	{
		for (FilterNode& node : newNodes)
			node.dbGain -= maxGain;

		plot->setNodes(newNodes);
		syncReadout();
		emit updateModel();
	}
}

void GraphicEQCardEditor::resetTriggered()
{
	// The legacy rule: an active selection resets only those bands,
	// otherwise the whole response flattens.
	vector<FilterNode> newNodes = plot->nodes();
	const QSet<int> selected = plot->selectedNodes();

	int index = 0;
	for (FilterNode& node : newNodes)
	{
		if (selected.isEmpty() || selected.contains(index))
			node.dbGain = 0.0;
		index++;
	}

	plot->setNodes(newNodes);
	syncReadout();
	emit updateModel();
}
