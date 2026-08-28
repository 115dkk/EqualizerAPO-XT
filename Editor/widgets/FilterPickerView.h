/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The skinnable "add filter" picker. FilterTable::chooseFilterTemplate used
	to open one flat search palette that listed every template at once, which
	read as noise. The picker is now a compact, dropdown-like popup anchored at
	the add button, and each skin can contribute its own FilterPickerView so
	the control matches the skin's design language (ISkin::createFilterPicker).
*/

#pragma once

#include <functional>

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "FilterPickerModel.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;

// A short, translated explanation of what a template does, keyed off the
// config line it inserts (command words are stable; translated names are not,
// the same reasoning the Soft picker uses to key its tile pictograms). Empty
// for an unrecognised line, so a skin can fall back to its own phrasing and a
// future template degrades gracefully instead of showing nothing. Skins show
// this in place of the raw config line, which reads as noise to anyone who is
// not editing the syntax by hand.
QString filterTemplateDescription(const QString& line);

// Base class for the skin-specific picker widget. The host embeds it in a
// frameless Qt::Popup container, calls setEntries() once, and runs a local
// event loop until entryChosen(index into that entries list) or dismissed().
// The container already closes on outside clicks and Esc; the view only has
// to present the entries and report a choice.
class FilterPickerView : public QWidget
{
	Q_OBJECT

public:
	// Showcase states the offscreen skin gallery asks a picker to present.
	// Default no-op: a picker that does not implement a state simply renders
	// its normal look, so the gallery's shot count stays deterministic while
	// each skin round implements its own staging.
	enum class GalleryShowcase
	{
		HoverFirstEntry,
		EmptySearch,
		// A search that lands on the Phase & Time group. The shared match
		// predicate includes the category in what it searches, so one term
		// brings back Delay and both all-pass sections together - which is the
		// only way a gallery shot can show where those templates went, rather
		// than only that they left the parametric list.
		PhaseAndTimeSearch
	};

	explicit FilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries);
	virtual void galleryShowcase(GalleryShowcase kind);

protected:
	virtual void entriesChanged() = 0;

	const QList<FilterPickerEntry>& pickerEntries() const;
	QList<FilterPickerMatch> pickerMatches() const;
	const QString& pickerQuery() const;
	void setPickerQuery(const QString& query);

	// Shared scaffolding for presentations backed by one search field and one
	// QListWidget. The subclass still creates every row and owns its delegate.
	void bindListPicker(
		QLineEdit* searchEdit,
		QListWidget* listWidget,
		int originalIndexRole,
		std::function<void()> rebuildList);
	void selectFirstListEntry();
	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	void entryChosen(int index);
	void dismissed();

private:
	void activateListItem(QListWidgetItem* item);

	FilterPickerModel pickerModel;
	QLineEdit* boundSearchEdit = nullptr;
	QListWidget* boundListWidget = nullptr;
	int boundOriginalIndexRole = Qt::UserRole;
};

// Neutral default, used by skins without a picker of their own: a search
// field over one sectioned column (category captions with that category's
// templates beneath), like a long structured dropdown.
class DefaultFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit DefaultFilterPickerView(QWidget* parent = nullptr);

	void galleryShowcase(GalleryShowcase kind) override;

protected:
	void entriesChanged() override;

private:
	void rebuildList();

	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
};
