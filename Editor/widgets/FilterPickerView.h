/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The skinnable "add filter" picker. FilterTable::chooseFilterTemplate used
	to open one flat search palette that listed every template at once, which
	read as noise. The picker is now a compact, dropdown-like popup anchored at
	the add button, and each skin can contribute its own FilterPickerView so
	the control matches the skin's design language (ISkin::createFilterPicker).
*/

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLineEdit;
class QListWidget;

// One insertable template, flattened from the filter GUI factories.
struct FilterPickerEntry
{
	QStringList path;   // category path, e.g. ["Parametric filters"]
	QString name;       // display name, e.g. "Peaking filter"
	QString line;       // the config line the template inserts
	QString description; // one-line explanation of what the template does,
	                     // empty for templates the catalog does not yet describe
};

// A short, translated explanation of what a template does, keyed off the
// config line it inserts (command words are stable; translated names are not,
// the same reasoning the Soft picker uses to key its tile pictograms). Empty
// for an unrecognised line, so a skin can fall back to its own phrasing and a
// future template degrades gracefully instead of showing nothing. Skins show
// this in place of the raw config line, which reads as noise to anyone who is
// not editing the syntax by hand.
QString filterTemplateDescription(const QString& line);
QString filterPickerSection(const FilterPickerEntry& entry);
bool filterPickerMatches(const FilterPickerEntry& entry, const QString& section, const QString& query);

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
		EmptySearch
	};

	explicit FilterPickerView(QWidget* parent = nullptr);

	virtual void setEntries(const QList<FilterPickerEntry>& entries) = 0;
	virtual void galleryShowcase(GalleryShowcase kind);

signals:
	void entryChosen(int index);
	void dismissed();
};

// Neutral default, used by skins without a picker of their own: a search
// field over one sectioned column (category captions with that category's
// templates beneath), like a long structured dropdown.
class DefaultFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit DefaultFilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries) override;
	void galleryShowcase(GalleryShowcase kind) override;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void rebuildList();
	void chooseCurrent();

	QList<FilterPickerEntry> allEntries;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
};
