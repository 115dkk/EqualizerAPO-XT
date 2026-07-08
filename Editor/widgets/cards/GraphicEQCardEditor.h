#pragma once

#include <QRegularExpression>
#include <QWidget>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "Editor/guis/GraphicEQFilterGUIView.h"
#include "helpers/GainIterator.h"

class FilterTable;
class GraphicEQFilterGUIScene;
class QComboBox;
class QTableWidget;
class QToolButton;

// The response-curve view of the modern GraphicEQ card. Reuses the legacy
// scene/view interaction stack (node drag, double-click insert, rubber-band
// select, Delete) but pins the horizontal axis to the card: the full 20 Hz -
// 20 kHz range always fits the viewport width, so the card never grows a
// horizontal scrollbar (the 960px gallery gate). The wheel zooms the dB axis
// only.
class GraphicEQCardPlotView : public GraphicEQFilterGUIView
{
	Q_OBJECT

public:
	explicit GraphicEQCardPlotView(QWidget* parent = nullptr);

	// The dB value at the viewport's top edge. The view re-applies it after
	// every width refit (setZoom resets scroll offsets), so the card frames
	// the response instead of the scene's +100 dB corner. User scrolling and
	// wheel zoom update it through scrollContentsBy.
	void setVerticalFrame(double topDb);
	double verticalFrameTopDb() const;

protected:
	void resizeEvent(QResizeEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void scrollContentsBy(int dx, int dy) override;

private:
	void fitWidth();
	void applyFrame();

	double frameTopDb = 21.0;
	bool applyingFrame = false;
};

// Modern card editor for "GraphicEQ:" lines - the first thing a clean
// install shows, so it replaces the promoted legacy GUI (stock radio
// buttons, unskinned table, .ico toolbar, fixed 1000px scene) in the card
// path. The legacy GraphicEQFilterGUI stays untouched for the frozen
// LegacyRows presentation. Skins style the chrome via object names
// (GraphicEQModeCombo, GraphicEQBandTable, GraphicEQActionButton) and the
// plot through the token-driven FrequencyPlotColors.
class GraphicEQCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	GraphicEQCardEditor(const std::vector<FilterNode>& nodes, const QString& configPath, FilterTable* filterTable, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

private slots:
	void insertRow(int index, double hz, double db);
	void removeRow(int index);
	void updateRow(int index, double hz, double db);
	void moveRow(int fromIndex, int toIndex);
	void selectRow(int index, bool select);
	void tableCellChanged(int row, int column);
	void tableSelectionChanged();
	void modeSelected(int comboIndex);
	void importTriggered();
	void exportTriggered();
	void invertTriggered();
	void normalizeTriggered();
	void resetTriggered();

private:
	void setFreqEditable(bool editable);
	void applyBandCount(int bandCount);
	void syncModeCombo(int bandCount);
	void applySkinToPlot();
	void retintActions();

	GraphicEQCardPlotView* plotView = nullptr;
	GraphicEQFilterGUIScene* scene = nullptr;
	QComboBox* modeCombo = nullptr;
	QTableWidget* bandTable = nullptr;
	QToolButton* importButton = nullptr;
	QToolButton* exportButton = nullptr;
	QToolButton* invertButton = nullptr;
	QToolButton* normalizeButton = nullptr;
	QToolButton* resetButton = nullptr;
	QString configPath;
	static QRegularExpression numberRegEx;
};
