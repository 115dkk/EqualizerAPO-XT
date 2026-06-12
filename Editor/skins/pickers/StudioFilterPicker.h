/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass "add filter" picker: a floating frosted-glass panel. The
	widget paints its own stage (the deep studio background), a soft
	elevation shadow, an accent glow behind the panel and a luminous top
	edge; inside sit a prominent sunken-glass search field and one airy
	sectioned list whose hovered entry pools light under the cursor and
	whose selected entry carries the skin's signal lamp.
*/

#pragma once

#include <QList>

#include "Editor/SkinTokens.h"
#include "Editor/widgets/FilterPickerView.h"

class QLineEdit;
class QListWidget;

class StudioFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit StudioFilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries) override;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void rebuildList();
	void chooseCurrent();

	QList<FilterPickerEntry> allEntries;
	SkinTokens skinTokens;
	bool dark = true;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
};
