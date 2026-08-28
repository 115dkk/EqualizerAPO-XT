/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "FilterPickerView.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QVBoxLayout>

#include "Editor/helpers/GUIHelper.h"
#include "FilterCommandCatalog.h"

QString filterTemplateDescription(const QString& rawLine)
{
	const QString line = rawLine.trimmed();
	if (line.isEmpty())
		return QString();
	if (line.startsWith(QLatin1Char('#')))
	{
		const FilterCommandCatalog::CommandEntry* comment
			= FilterCommandCatalog::entryForKeyword(QStringLiteral("#"));
		return comment == nullptr ? QString() : FilterCommandCatalog::description(*comment);
	}

	const int colon = line.indexOf(QLatin1Char(':'));
	const QString command = (colon > 0 ? line.left(colon) : line).trimmed();

	// Biquad templates all share the "Filter" command, so split further on the
	// type token to give each response shape its own line (the catalog's curve
	// table, the same one the pictograms use).
	if (command == QLatin1String("Filter"))
	{
		// The order distinguishes the two all-pass entries, which share a type
		// token and would otherwise read identically in the picker.
		if (line.contains(QLatin1String(" AP ")) && line.contains(QLatin1String("Order 1")))
			return FilterCommandCatalog::firstOrderAllPassDescription();
		for (const FilterCommandCatalog::BiquadCurveEntry& curve
			: FilterCommandCatalog::biquadCurves())
			if (line.contains(QStringLiteral(" %1 ").arg(QLatin1String(curve.code))))
				return FilterCommandCatalog::curveDescription(curve);
		return QString();
	}

	// Exact-match on the canonical spelling, like the engine's own lookup: a
	// lowercase "preamp:" is a note, not a command, and gets no description.
	const FilterCommandCatalog::CommandEntry* entry
		= FilterCommandCatalog::entryForKeyword(command);
	return entry == nullptr ? QString() : FilterCommandCatalog::description(*entry);
}

FilterPickerView::FilterPickerView(QWidget* parent)
	: QWidget(parent)
{
}

void FilterPickerView::setEntries(const QList<FilterPickerEntry>& entries)
{
	pickerModel.setEntries(entries);
	entriesChanged();
}

const QList<FilterPickerEntry>& FilterPickerView::pickerEntries() const
{
	return pickerModel.entries();
}

QList<FilterPickerMatch> FilterPickerView::pickerMatches() const
{
	return pickerModel.matches();
}

const QString& FilterPickerView::pickerQuery() const
{
	return pickerModel.query();
}

void FilterPickerView::setPickerQuery(const QString& query)
{
	pickerModel.setQuery(query);
}

void FilterPickerView::bindListPicker(
	QLineEdit* searchEdit,
	QListWidget* listWidget,
	int originalIndexRole,
	std::function<void()> rebuildList)
{
	boundSearchEdit = searchEdit;
	boundListWidget = listWidget;
	boundOriginalIndexRole = originalIndexRole;
	boundSearchEdit->installEventFilter(this);

	connect(boundSearchEdit, &QLineEdit::textChanged, this,
		[this, rebuildList](const QString& query) {
			setPickerQuery(query);
			rebuildList();
		});
	connect(boundListWidget, &QListWidget::currentItemChanged, this,
		[this](QListWidgetItem* current) {
			if (current != nullptr && (current->flags() & Qt::ItemIsSelectable))
				pickerModel.selectIndex(current->data(boundOriginalIndexRole).toInt());
		});
	connect(boundListWidget, &QListWidget::itemClicked,
		this, &FilterPickerView::activateListItem);
	connect(boundListWidget, &QListWidget::itemActivated,
		this, &FilterPickerView::activateListItem);
}

void FilterPickerView::selectFirstListEntry()
{
	if (boundListWidget == nullptr)
		return;
	for (int row = 0; row < boundListWidget->count(); row++)
	{
		if (boundListWidget->item(row)->flags() & Qt::ItemIsSelectable)
		{
			boundListWidget->setCurrentRow(row);
			return;
		}
	}
}

void FilterPickerView::activateListItem(QListWidgetItem* item)
{
	if (item == nullptr || !(item->flags() & Qt::ItemIsSelectable))
		return;
	const int originalIndex = item->data(boundOriginalIndexRole).toInt();
	if (pickerModel.selectIndex(originalIndex))
		emit entryChosen(originalIndex);
}

bool FilterPickerView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == boundSearchEdit && event->type() == QEvent::KeyPress)
	{
		QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
		switch (keyEvent->key())
		{
		case Qt::Key_Down:
		case Qt::Key_Up:
		case Qt::Key_PageDown:
		case Qt::Key_PageUp:
			QApplication::sendEvent(boundListWidget, event);
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			activateListItem(boundListWidget->currentItem());
			return true;
		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void FilterPickerView::galleryShowcase(GalleryShowcase)
{
}

DefaultFilterPickerView::DefaultFilterPickerView(QWidget* parent)
	: FilterPickerView(parent)
{
	setObjectName(QStringLiteral("FilterPicker"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	searchEdit = new QLineEdit(this);
	searchEdit->setObjectName(QStringLiteral("FilterPickerSearch"));
	searchEdit->setPlaceholderText(tr("Search filters"));
	searchEdit->setClearButtonEnabled(true);
	// Arrow keys and Return typed in the search field drive the list below, so
	// keyboard users never have to leave the field.
	layout->addWidget(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("FilterPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setUniformItemSizes(false);
	layout->addWidget(listWidget, 1);
	bindListPicker(searchEdit, listWidget, Qt::UserRole, [this]() { rebuildList(); });

	setMinimumWidth(GUIHelper::scale(300.0));
	setMaximumHeight(GUIHelper::scale(420.0));
}

void DefaultFilterPickerView::entriesChanged()
{
	rebuildList();
	searchEdit->setFocus();
}

void DefaultFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::EmptySearch)
	{
		// A term that matches no template: the gallery captures what the user
		// sees after a fruitless search.
		searchEdit->setText(QStringLiteral("zzzz"));
		return;
	}

	searchEdit->clear();
	for (int row = 0; row < listWidget->count(); row++)
	{
		QListWidgetItem* item = listWidget->item(row);
		if (!(item->flags() & Qt::ItemIsSelectable))
			continue;
		// Hover is driven by real mouse events (the view keeps a hover index
		// updated from MouseMove); feed it a synthetic move over the first
		// entry so the offscreen render shows the hover styling.
		listWidget->viewport()->setAttribute(Qt::WA_UnderMouse, true);
		const QPointF center = listWidget->visualItemRect(item).center();
		QMouseEvent moveEvent(QEvent::MouseMove, center,
			listWidget->viewport()->mapToGlobal(center),
			Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(listWidget->viewport(), &moveEvent);
		listWidget->viewport()->update();
		break;
	}
}

void DefaultFilterPickerView::rebuildList()
{
	listWidget->clear();

	QString currentSection;
	bool sectionStarted = false;
	for (const FilterPickerMatch& match : pickerMatches())
	{
		const FilterPickerEntry& entry = pickerEntries()[match.originalIndex];
		const QString& section = match.section;

		if (!sectionStarted || section != currentSection)
		{
			sectionStarted = true;
			currentSection = section;
			QListWidgetItem* caption = new QListWidgetItem(section, listWidget);
			caption->setFlags(Qt::NoItemFlags);
			QFont captionFont = caption->font();
			captionFont.setBold(true);
			captionFont.setPointSizeF(captionFont.pointSizeF() * 0.9);
			caption->setFont(captionFont);
		}

		QListWidgetItem* item = new QListWidgetItem(entry.name, listWidget);
		item->setData(Qt::UserRole, match.originalIndex);
		item->setToolTip(entry.line);
	}

	// Preselect the first real entry so Return inserts immediately.
	selectFirstListEntry();
}
