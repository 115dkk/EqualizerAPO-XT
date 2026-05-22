#pragma once

#include <QFileInfo>

#include "Editor/IFilterGUI.h"

class FilterTable;
class QLabel;
class QLineEdit;
class QToolButton;

class IncludeCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void chooseFile();
	void openFile();
	void pathEdited();
	void importToConfig();

private:
	QFileInfo currentFileInfo() const;
	void updateFileInfo();

	FilterTable* filterTable = nullptr;
	QLineEdit* pathEdit = nullptr;
	QLabel* statusLabel = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* openButton = nullptr;
	QToolButton* importButton = nullptr;
};
