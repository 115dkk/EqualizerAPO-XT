/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab's "add filter" picker: a rounded menu card in the language of a
	consumer settings app. A pill search field sits over comfortable two-line
	rows (template name plus a friendly caption - the config line, or a calm
	promise when the template inserts a bare command), each led by an
	iOS-Settings-style rounded-square colour tile in its category's pastel
	carrying a per-item monogram (AR1 F4: single initials collided, e.g.
	Comment/Channel/Copy all "C"); category headers are tinted pills of the
	same hue. The hovered row lifts one value step and the current row gets a
	fully rounded stadium highlight, echoing the skin's chip grammar. A
	fruitless search shows a friendly empty-state card instead of a bare
	void. Elevation is faked with one background value step under the card
	plus the usual very light 1px border.
*/

#pragma once

#include <QColor>
#include <QHash>
#include <QStringList>

#include "Editor/widgets/FilterPickerView.h"

class QLineEdit;
class QListWidget;

class SoftFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit SoftFilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries) override;
	void galleryShowcase(GalleryShowcase kind) override;

	QSize sizeHint() const override;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void rebuildList();
	void chooseCurrent();

	QList<FilterPickerEntry> allEntries;
	// Per-entry tile monograms, parallel to allEntries; computed once per
	// catalog so a search never re-letters the tiles.
	QStringList entryMonograms;
	// Category -> pastel tile colour, assigned in catalog order so a category
	// keeps its hue however the search narrows the list.
	QHash<QString, QColor> sectionColors;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
	int listContentHeight = 0;
};
