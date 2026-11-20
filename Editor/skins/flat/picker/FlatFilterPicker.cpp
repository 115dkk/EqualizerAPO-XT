/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FlatFilterPicker.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/flat/FlatPaint.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/FilterCardModel.h"

namespace
{
enum FlatPickerRole
{
	// Original index into the entries list; -1 for sections / the empty state.
	EntryIndexRole = Qt::UserRole,
	// Entry name or section label.
	TitleRole,
	// The friendly caption under the name (entries and the empty state).
	CaptionRole,
	// FlatPickerItemKind.
	KindRole,
	// The per-item tile monogram (entries only; the fallback glyph).
	GlyphRole,
	// The per-item tile pictogram resource (entries only; empty falls back
	// to the monogram).
	IconRole
};

enum FlatPickerItemKind
{
	EntryItem = 0,
	SectionItem,
	EmptyStateItem
};

// Per-item tile monograms (single initials collide: Comment, Channel, Copy
// and Convolution would all wear "C"). Multi-word names take their first two
// word initials ("Low-pass filter" -> "LP"), single-word names their first
// two letters ("Channel" -> "Ch"), and a catalog-wide uniqueness pass walks
// the remaining letters of a single-word name whose candidate is taken
// (Comment keeps "Co", Copy becomes "Cp", Convolution "Cn"). Deterministic
// in catalog order; the category pastel stays the second disambiguator.
QStringList flatMonograms(const QList<FilterPickerEntry>& entries)
{
	QSet<QString> used;
	QStringList result;
	for (const FilterPickerEntry& entry : entries)
	{
		// The parenthetical part of a template name is a description, not a
		// name ("Channel (Select channels)"), so it lends no letters.
		QString base = entry.name;
		const int paren = base.indexOf(QLatin1Char('('));
		if (paren > 0)
			base = base.left(paren);
		const QStringList words = base.split(
			QRegularExpression(QStringLiteral("[^\\p{L}\\p{Nd}]+")), Qt::SkipEmptyParts);

		QString glyph = QStringLiteral("?");
		if (words.size() >= 2)
			glyph = QString(words[0].at(0).toUpper()) + words[1].at(0).toUpper();
		else if (!words.isEmpty() && words[0].size() >= 2)
			glyph = QString(words[0].at(0).toUpper()) + words[0].at(1).toLower();
		else if (!words.isEmpty())
			glyph = words[0].toUpper();

		if (used.contains(glyph) && words.size() == 1)
		{
			for (int i = 2; i < words[0].size(); i++)
			{
				const QString alternative = QString(words[0].at(0).toUpper()) + words[0].at(i).toLower();
				if (!used.contains(alternative))
				{
					glyph = alternative;
					break;
				}
			}
		}
		used.insert(glyph);
		result.append(glyph);
	}
	return result;
}

// The tile pictogram for a catalog entry, keyed off the template line the
// entry inserts (names are translated and lend no stable key; command words
// are not). Biquad templates split further by their type token, so every EQ
// shape carries its own response-curve glyph. An unmapped template returns
// empty and the tile falls back to its monogram, so future catalog entries
// degrade gracefully instead of going blank.
QString flatEntryIcon(const FilterPickerEntry& entry)
{
	const QString line = entry.line.trimmed();
	if (line.startsWith(QLatin1Char('#')))
		return FilterCardModel::commandIconResource(QStringLiteral("#"));

	const int colon = line.indexOf(QLatin1Char(':'));
	const QString command = colon > 0 ? line.left(colon).trimmed() : QString();
	const QString parameters = colon >= 0 ? line.mid(colon + 1) : QString();
	return FilterCardModel::commandIconResource(command, parameters);
}

// Templates that insert a bare command ("Include:", "Copy: ", "# ") would
// print that fragment as the caption, which reads as a truncated line. The
// display layer swaps empty and colon-ended previews for a calm promise; the
// raw line keeps living in the tooltip and in what the choice actually
// inserts, so nothing is hidden, only phrased kindly.
QString flatCaption(const QString& line)
{
	const QString display = line.trimmed();
	// Else:/EndIf: are complete lines rather than fragments awaiting details;
	// promising details to choose would be a small lie, so the block closers
	// carry their own calm note.
	if (display == QStringLiteral("Else:") || display == QStringLiteral("EndIf:"))
		return QCoreApplication::translate("FlatFilterPickerView", "Complete as it is");
	if (display.isEmpty() || display.endsWith(QLatin1Char(':')) || display == QStringLiteral("#"))
		return QCoreApplication::translate("FlatFilterPickerView", "Choose the details after adding");
	return display;
}

// Paints the list rows the way a menu is painted: a section name in the quiet
// ink, then rows of a glyph, a name and a line about it. The current row takes
// the neutral selection step; nothing is tinted.
class FlatPickerDelegate : public QStyledItemDelegate
{
public:
	FlatPickerDelegate(const SkinTokens& tokens, QObject* parent)
		: QStyledItemDelegate(parent), skinTokens(tokens)
	{
	}

	const SkinTokens skinTokens;

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		Q_UNUSED(option);
		switch (index.data(KindRole).toInt())
		{
		case SectionItem:
			// The first section sits right under the search field; later ones
			// keep a gap above them, which is all that separates the sections.
			return QSize(100, (index.row() == 0 ? 26 : 36));
		case EmptyStateItem:
			return QSize(100, 96);
		default:
			return QSize(100, 52);
		}
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		const SkinTokens& t = skinTokens;
		const QString title = index.data(TitleRole).toString();

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setRenderHint(QPainter::TextAntialiasing, true);
		switch (index.data(KindRole).toInt())
		{
		case SectionItem:
			paintSection(painter, option, t, title);
			break;
		case EmptyStateItem:
			paintEmptyState(painter, option, index, t, title);
			break;
		default:
			paintEntry(painter, option, index, t, title);
			break;
		}
		painter->restore();
	}

private:
	static void paintSection(QPainter* painter, const QStyleOptionViewItem& option, const SkinTokens& t,
		const QString& title)
	{
		const QFont font = FlatPaint::bodyFont(t, 13, QFont::DemiBold);
		const QRectF rect(option.rect.left() + 12.0, option.rect.bottom() - 23.0, option.rect.width() - 24.0, 22.0);
		painter->setFont(font);
		painter->setPen(QColor(t.mutedText));
		painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetricsF(font).elidedText(title, Qt::ElideRight, rect.width()));
	}

	// Nothing matched: a magnifier in the quiet ink, what happened, and what
	// to try. No card of its own; the popup is the card.
	static void paintEmptyState(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
		const SkinTokens& t, const QString& title)
	{
		const QString caption = index.data(CaptionRole).toString();
		const QRectF area(option.rect);

		const QPointF lensCenter(area.center().x() - 2.0, area.top() + 22.0);
		painter->setPen(QPen(QColor(t.mutedText), 1.5, Qt::SolidLine, Qt::RoundCap));
		painter->setBrush(Qt::NoBrush);
		painter->drawEllipse(lensCenter, 6.0, 6.0);
		painter->drawLine(lensCenter + QPointF(4.5, 4.5), lensCenter + QPointF(9.0, 9.0));

		const QFont nameFont = FlatPaint::bodyFont(t, 14, QFont::DemiBold);
		const QFont captionFont = FlatPaint::bodyFont(t, 13);
		painter->setFont(nameFont);
		painter->setPen(QColor(t.text));
		painter->drawText(QRectF(area.left(), area.top() + 40.0, area.width(), 22.0),
			Qt::AlignHCenter | Qt::AlignVCenter, title);
		painter->setFont(captionFont);
		painter->setPen(QColor(t.mutedText));
		painter->drawText(QRectF(area.left(), area.top() + 62.0, area.width(), 20.0),
			Qt::AlignHCenter | Qt::AlignVCenter, caption);
	}

	static void paintEntry(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
		const SkinTokens& t, const QString& title)
	{
		QRectF row(option.rect);
		row.adjust(0, 1, 0, -1);
		// One emphasis: the pointer and the arrow keys move the same current
		// row (shared article 7).
		if (option.state.testFlag(QStyle::State_Selected))
			FlatPaint::box(*painter, row, QColor(t.cardSelected), QColor(), FlatPaint::controlRadius, 0.0);

		// The command's pictogram in the body ink; a template that has none
		// falls back to its letters.
		const qreal glyphSide = 18.0;
		const QRectF glyphBox(row.left() + 14.0, row.center().y() - glyphSide / 2.0, glyphSide, glyphSide);
		const QString iconResource = index.data(IconRole).toString();
		if (!iconResource.isEmpty())
		{
			GUIHelper::tintedIcon(iconResource, QColor(t.text), int(glyphSide))
				.paint(painter, glyphBox.toRect());
		}
		else
		{
			painter->setFont(FlatPaint::bodyFont(t, 12, QFont::Bold));
			painter->setPen(QColor(t.text));
			painter->drawText(glyphBox.adjusted(-4.0, 0.0, 4.0, 0.0), Qt::AlignCenter, index.data(GlyphRole).toString());
		}

		const QString caption = index.data(CaptionRole).toString();
		const qreal textLeft = glyphBox.right() + 14.0;
		const qreal textWidth = row.right() - 12.0 - textLeft;
		const QFont nameFont = FlatPaint::bodyFont(t, 14, QFont::DemiBold);
		const QFontMetricsF nameMetrics(nameFont);
		const QFont captionFont = FlatPaint::bodyFont(t, 13);
		const QFontMetricsF captionMetrics(captionFont);
		const qreal textHeight = nameMetrics.height() + (caption.isEmpty() ? 0.0 : captionMetrics.height());
		const qreal textTop = row.center().y() - textHeight / 2.0;

		painter->setFont(nameFont);
		painter->setPen(QColor(t.text));
		painter->drawText(QRectF(textLeft, textTop, textWidth, nameMetrics.height()),
			Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(title, Qt::ElideRight, textWidth));
		if (!caption.isEmpty())
		{
			painter->setFont(captionFont);
			painter->setPen(QColor(t.mutedText));
			painter->drawText(QRectF(textLeft, textTop + nameMetrics.height(), textWidth, captionMetrics.height()),
				Qt::AlignLeft | Qt::AlignVCenter, captionMetrics.elidedText(caption, Qt::ElideRight, textWidth));
		}
	}
};
}

FlatFilterPickerView::FlatFilterPickerView(const SkinTokens& tokens, QWidget* parent)
	: FilterPickerView(parent),
	  skinTokens(tokens)
{
	setObjectName(QStringLiteral("FlatFilterPicker"));

	QVBoxLayout* layout = new QVBoxLayout(this);
	const int pad = 10;
	layout->setContentsMargins(pad, pad, pad, pad);
	layout->setSpacing(8);

	searchEdit = new QLineEdit(this);
	searchEdit->setObjectName(QStringLiteral("FlatPickerSearch"));
	searchEdit->setPlaceholderText(tr("Search filters"));
	searchEdit->setClearButtonEnabled(true);
	// Arrow keys and Return typed in the pill drive the list below, so
	// keyboard users never have to leave the field.
	layout->addWidget(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("FlatPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	listWidget->setUniformItemSizes(false);
	listWidget->setItemDelegate(new FlatPickerDelegate(skinTokens, listWidget));
	layout->addWidget(listWidget, 1);
	bindListPicker(searchEdit, listWidget, EntryIndexRole, [this]() { rebuildList(); });

	// Roomy and approachable: the widest, tallest-rowed picker of the five.
	setFixedWidth(380);
	setMaximumHeight(470);
}

void FlatFilterPickerView::entriesChanged()
{
	entryMonograms = flatMonograms(pickerEntries());
	rebuildList();
	searchEdit->setFocus();
}

void FlatFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::PhaseAndTimeSearch)
	{
		// Where Delay and the two all-pass sections went. The category is
		// part of what the shared predicate searches, so one term returns
		// the whole group.
		searchEdit->setText(QStringLiteral("phase"));
		return;
	}
	if (kind == GalleryShowcase::ControlSearch)
	{
		// The names that used to carry a parenthetical in the bold title.
		searchEdit->setText(QStringLiteral("control"));
		return;
	}
	if (kind == GalleryShowcase::EmptySearch)
	{
		// What the user sees after a fruitless search: the friendly
		// empty-state card under the pill (the delegate's EmptyStateItem).
		searchEdit->setText(QStringLiteral("zzzz"));
		return;
	}

	searchEdit->clear();
	// The first selectable row is already the preselected stadium; the
	// pointer rests on the entry after it, and the stadium follows it there.
	QListWidgetItem* target = nullptr;
	int selectableSeen = 0;
	for (int row = 0; row < listWidget->count(); row++)
	{
		QListWidgetItem* item = listWidget->item(row);
		if (!(item->flags() & Qt::ItemIsSelectable))
			continue;
		target = item;
		if (++selectableSeen == 2)
			break;
	}
	if (target == nullptr)
		return;

	// The pointer is driven by real mouse events; feed the viewport a
	// synthetic move over the target so the offscreen render follows it.
	listWidget->viewport()->setAttribute(Qt::WA_UnderMouse, true);
	const QPointF center = listWidget->visualItemRect(target).center();
	QMouseEvent moveEvent(QEvent::MouseMove, center,
		listWidget->viewport()->mapToGlobal(center),
		Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(listWidget->viewport(), &moveEvent);
	listWidget->viewport()->update();
}

void FlatFilterPickerView::rebuildList()
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
			QListWidgetItem* caption = new QListWidgetItem(listWidget);
			caption->setFlags(Qt::NoItemFlags);
			caption->setData(EntryIndexRole, -1);
			caption->setData(TitleRole, section);
			caption->setData(KindRole, SectionItem);
		}

		// The name carries the row on its own; its parenthetical description
		// is what the caption below already says, so the title drops it (the
		// bold name used to elide at "NewChannel (Create virtual channe...").
		const FilterPickerTitleParts parts = filterPickerTitleParts(entry.name);
		QListWidgetItem* item = new QListWidgetItem(listWidget);
		item->setData(EntryIndexRole, match.originalIndex);
		item->setData(TitleRole, parts.title);
		// A calm sentence about what the filter does, not the raw config line.
		// The catalog describes every current template; the name's own note
		// and then flatCaption stay the fallbacks so a future, undescribed
		// template still reads kindly.
		QString caption = entry.description;
		if (caption.isEmpty())
			caption = parts.note.isEmpty() ? flatCaption(entry.line) : parts.note;
		item->setData(CaptionRole, caption);
		item->setData(KindRole, EntryItem);
		item->setData(GlyphRole, entryMonograms.value(match.originalIndex, entry.name.left(1).toUpper()));
		item->setData(IconRole, flatEntryIcon(entry));
		// The tooltip keeps the raw template line even when the caption wears
		// the friendly phrasing - what gets inserted is never hidden.
		item->setToolTip(entry.line);
	}

	// A friendly, quiet empty-state card instead of a bare void.
	if (listWidget->count() == 0)
	{
		QListWidgetItem* empty = new QListWidgetItem(listWidget);
		empty->setFlags(Qt::NoItemFlags);
		empty->setData(EntryIndexRole, -1);
		empty->setData(TitleRole, tr("Nothing matches your search"));
		empty->setData(CaptionRole, tr("Try a shorter or different keyword"));
		empty->setData(KindRole, EmptyStateItem);
	}

	// Preselect the first real entry so Return inserts immediately.
	selectFirstListEntry();

	listContentHeight = 0;
	for (int row = 0; row < listWidget->count(); row++)
		listContentHeight += listWidget->sizeHintForRow(row);
	updateGeometry();
}

QSize FlatFilterPickerView::sizeHint() const
{
	const QMargins margins = layout()->contentsMargins();
	int height = margins.top() + margins.bottom() + layout()->spacing()
		+ searchEdit->sizeHint().height() + listContentHeight + 4;
	height = qMin(height, maximumHeight());
	return QSize(380, height);
}

void FlatFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	const SkinTokens& t = skinTokens;

	QPainter painter(this);
	// Backing for the rounded corners: the window background, which is also
	// what the popup floats over.
	painter.fillRect(rect(), QColor(t.background));
	// A floating surface: the card colour inside a line one step heavier than
	// a card's, at the surface radius.
	FlatPaint::box(painter, QRectF(rect()), QColor(t.card), QColor(t.borderStrong), FlatPaint::surfaceRadius);
}
