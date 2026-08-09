/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The "add filter" picker of the rack skin: a 1U module preset browser
	painted as a brushed faceplate with an LCD search strip and LED slots.
	Constitution: docs/skins/rack.md ("필터 픽커" section).
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

	void galleryShowcase(GalleryShowcase kind) override;

	QSize sizeHint() const override;

protected:
	void entriesChanged() override;
	void paintEvent(QPaintEvent* event) override;

private:
	void rebuildList();

	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
	// Natural (uncapped) pixel height of the current list content, kept by
	// rebuildList so sizeHint can size the popup like a real 1U module:
	// exactly as tall as its slots, up to the rack's height limit.
	int listContentHeight = 0;
};
