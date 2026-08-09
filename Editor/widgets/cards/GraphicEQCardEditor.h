#pragma once

#include <QRegularExpression>
#include <QWidget>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "filters/graphicEq/GainIterator.h"

class FilterTable;
class GraphicEQPlotWidget;
class QComboBox;
class QLabel;
class QToolButton;
class ValueScrubBox;

// Modern card editor for "GraphicEQ:" lines - the first thing a clean
// install shows. Form before color: the response lives in a skin-painted
// GraphicEQPlotWidget (ISkin::paintGraphicEqPlot), and the legacy side table
// is replaced by a selected-band readout strip built from the established
// per-skin control grammars (value scrub for Freq/Gain, paramSelector for
// the band layout). The frozen LegacyRows presentation keeps the original
// GraphicEQFilterGUI untouched.
class GraphicEQCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	GraphicEQCardEditor(const std::vector<FilterNode>& nodes, const QString& configPath, FilterTable* filterTable, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

private slots:
	void modeSelected(int comboIndex);
	void focusedNodeChanged(int index);
	void readoutFreqChanged(double value);
	void readoutGainChanged(double value);
	void importTriggered();
	void exportTriggered();
	void invertTriggered();
	void normalizeTriggered();
	void resetTriggered();

private:
	void applyBandCount(int bandCount);
	void syncModeCombo(int bandCount);
	void syncReadout();
	void retintActions();
	static int verifyBands(const std::vector<FilterNode>& nodes);

	GraphicEQPlotWidget* plot = nullptr;
	QComboBox* modeCombo = nullptr;
	QLabel* bandCaption = nullptr;
	ValueScrubBox* freqBox = nullptr;
	ValueScrubBox* gainBox = nullptr;
	QToolButton* importButton = nullptr;
	QToolButton* exportButton = nullptr;
	QToolButton* invertButton = nullptr;
	QToolButton* normalizeButton = nullptr;
	QToolButton* resetButton = nullptr;
	QString configPath;
	bool syncingReadout = false;
	static QRegularExpression numberRegEx;
};
