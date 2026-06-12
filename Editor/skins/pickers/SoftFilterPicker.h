/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab's "add filter" picker: a rounded menu card in the language of a
	consumer settings app. A pill search field sits over comfortable two-line
	rows (template name plus its config line as a muted caption), each led by
	an iOS-Settings-style rounded-square colour tile in its category's pastel;
	category headers are tinted pills of the same hue. The hovered row lifts
	one value step and the current row gets a fully rounded stadium highlight,
	echoing the skin's chip grammar. Elevation is faked with one background
	value step under the card plus the usual very light 1px border.
*/

#pragma once

#include <QColor>
#include <QHash>

#include "Editor/widgets/FilterPickerView.h"

class QLineEdit;
class QListWidget;

class SoftFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit SoftFilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries) override;

	QSize sizeHint() const override;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void rebuildList();
	void chooseCurrent();

	QList<FilterPickerEntry> allEntries;
	// Category -> pastel tile colour, assigned in catalog order so a category
	// keeps its hue however the search narrows the list.
	QHash<QString, QColor> sectionColors;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
	int listContentHeight = 0;
};
