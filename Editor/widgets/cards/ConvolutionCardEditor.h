#pragma once

#include "Editor/IFilterGUI.h"

class FilterTable;
class QLabel;
class QLineEdit;
class QToolButton;

// Modern card body for a "Convolution:" line, the card-mode counterpart of the
// legacy guis/ConvolutionFilterGUI. Mirrors IncludeCardEditor's file-path layout
// and shares its "import into the config directory" affordance, but keeps the
// impulse-response readout (length, sample rate, device-rate mismatch) that is
// specific to convolution.
class ConvolutionCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	ConvolutionCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void chooseFile();
	void pathEdited();
	void importToConfig();

private:
	QString resolvedAbsolutePath() const;
	unsigned currentDeviceSampleRate() const;
	void updateFileInfo();

	FilterTable* filterTable = nullptr;
	QLineEdit* pathEdit = nullptr;
	QLabel* infoLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* importButton = nullptr;
};
