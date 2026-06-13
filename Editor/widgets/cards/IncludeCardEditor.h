#pragma once

#include <QFileInfo>

#include "Editor/IFilterGUI.h"

class FilterTable;
class ReferenceCard;
class QToolButton;

class IncludeCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void openFile();
	void locateFile();
	void pathChanged(const QString& newPath);
	void importToConfig();

private:
	QFileInfo currentFileInfo() const;
	QFileInfo currentFileInfo(const QString& path) const;
	void updateFileInfo();
	// First few lines of the include target, for the jump affordance's hover
	// preview (X-8). Returns an empty string on any read failure.
	QString previewText() const;

	FilterTable* filterTable = nullptr;
	QString includePath;
	ReferenceCard* card = nullptr;
	QToolButton* locateButton = nullptr;
	QToolButton* importButton = nullptr;
	bool canOpen = false;
};
