/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The "add filter" picker of the rack skin: a hardware module's preset
	browser. A brushed 1U faceplate with corner screws and an engraved
	MODULE SELECT header, a recessed LCD character display as the search
	strip, categories as engraved section plates and every insertable
	template as a labeled slot with its own panel LED (lit = selected,
	faint lamp glow = hovered). All painting follows RackChrome's
	tiebreaker: only what a hardware faceplate would carry.
*/

#pragma once

#include <QList>

#include "Editor/widgets/FilterPickerView.h"

class QLineEdit;
class QListWidget;

class RackFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit RackFilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries) override;
	void galleryShowcase(GalleryShowcase kind) override;

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void rebuildList();
	void chooseCurrent();

	QList<FilterPickerEntry> allEntries;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
	// Natural (uncapped) pixel height of the current list content, kept by
	// rebuildList so sizeHint can size the popup like a real 1U module:
	// exactly as tall as its slots, up to the rack's height limit.
	int listContentHeight = 0;
};
