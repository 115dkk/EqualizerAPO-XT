/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftFilterPicker.h"
#include "Editor/skins/shared/SkinPaint.h"

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

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/FilterCardModel.h"

namespace
{
enum SoftPickerRole
{
	// Original index into the entries list; -1 for sections / the empty state.
	EntryIndexRole = Qt::UserRole,
	// Entry name or section label.
	TitleRole,
	// The friendly caption under the name (entries and the empty state).
	CaptionRole,
	// The category pastel (QColor).
	TintRole,
	// SoftPickerItemKind.
	KindRole,
	// The per-item tile monogram (entries only; the fallback glyph).
	GlyphRole,
	// The per-item tile pictogram resource (entries only; empty falls back
	// to the monogram).
	IconRole
};

enum SoftPickerItemKind
{
	EntryItem = 0,
	SectionItem,
	EmptyStateItem
};

// Pastel hues handed out to categories in catalog order.
QColor sectionPastel(int sectionIndex, bool dark)
{
	static const int hues[] = { 216, 150, 26, 268, 336, 190, 48, 0, 286, 120 };
	const int hue = hues[sectionIndex % int(sizeof(hues) / sizeof(hues[0]))];
	return QColor::fromHslF(hue / 360.0, dark ? 0.52 : 0.58, dark ? 0.64 : 0.56);
}

// Per-item tile monograms (single initials collide: Comment, Channel, Copy
// and Convolution would all wear "C"). Multi-word names take their first two
// word initials ("Low-pass filter" -> "LP"), single-word names their first
// two letters ("Channel" -> "Ch"), and a catalog-wide uniqueness pass walks
// the remaining letters of a single-word name whose candidate is taken
// (Comment keeps "Co", Copy becomes "Cp", Convolution "Cn"). Deterministic
// in catalog order; the category pastel stays the second disambiguator.
QStringList softMonograms(const QList<FilterPickerEntry>& entries)
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
QString softEntryIcon(const FilterPickerEntry& entry)
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
QString softCaption(const QString& line)
{
	const QString display = line.trimmed();
	// Else:/EndIf: are complete lines rather than fragments awaiting details;
	// promising details to choose would be a small lie, so the block closers
	// carry their own calm note.
	if (display == QStringLiteral("Else:") || display == QStringLiteral("EndIf:"))
		return QCoreApplication::translate("SoftFilterPickerView", "Complete as it is");
	if (display.isEmpty() || display.endsWith(QLatin1Char(':')) || display == QStringLiteral("#"))
		return QCoreApplication::translate("SoftFilterPickerView", "Choose the details after adding");
	return display;
}

// Paints the menu rows: stadium highlights, rounded-square colour tiles and
// pill section headers, all from the live skin tokens so both modes stay calm.
class SoftPickerDelegate : public QStyledItemDelegate
{
public:
	SoftPickerDelegate(const SkinTokens& tokens, QObject* parent)
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
			// Whitespace is the hierarchy device: every section after the
			// first carries its breathing room above the pill.
			return QSize(GUIHelper::scale(100.0), GUIHelper::scale(index.row() == 0 ? 26.0 : 38.0));
		case EmptyStateItem:
			// Room for the friendly empty-state card (glyph, title, caption).
			return QSize(GUIHelper::scale(100.0), GUIHelper::scale(112.0));
		default:
			return QSize(GUIHelper::scale(100.0), GUIHelper::scale(48.0));
		}
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		const SkinTokens& t = skinTokens;
		const bool dark = skinTokens.dark;
		const QString title = index.data(TitleRole).toString();

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);

		switch (index.data(KindRole).toInt())
		{
		case SectionItem:
			paintSection(painter, option, index, t, dark, title);
			break;
		case EmptyStateItem:
			paintEmptyState(painter, option, index, t, title);
			break;
		default:
			paintEntry(painter, option, index, t, dark, title);
			break;
		}

		painter->restore();
	}

private:
	static void paintSection(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
		const SkinTokens& t, bool dark, const QString& title)
	{
		const QColor tint = index.data(TintRole).value<QColor>();
		QFont pillFont = option.font;
		pillFont.setWeight(QFont::DemiBold);
		pillFont.setPointSizeF(option.font.pointSizeF() * 0.84);
		pillFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
		const QString label = title.toUpper();
		const QFontMetricsF metrics(pillFont);

		const qreal pillHeight = GUIHelper::scale(22.0);
		const qreal pillWidth = qMin<qreal>(option.rect.width() - GUIHelper::scale(12.0),
			metrics.horizontalAdvance(label) + GUIHelper::scale(24.0));
		const QRectF pill(option.rect.left() + GUIHelper::scale(6.0),
			option.rect.bottom() - pillHeight - GUIHelper::scale(1.0), pillWidth, pillHeight);

		painter->setPen(Qt::NoPen);
		painter->setBrush(withAlpha(tint, dark ? 46 : 40));
		painter->drawRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);
		painter->setFont(pillFont);
		painter->setPen(mixColor(tint, QColor(t.text), dark ? 0.42 : 0.40));
		painter->drawText(pill, Qt::AlignCenter, metrics.elidedText(label, Qt::ElideRight, pillWidth - GUIHelper::scale(16.0)));
	}

	// The fruitless search: a friendly card one value step above the menu
	// surface - a pastel circle with a painted magnifier, the title in full
	// ink, a muted caption, no warning colour anywhere.
	static void paintEmptyState(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
		const SkinTokens& t, const QString& title)
	{
		const QString caption = index.data(CaptionRole).toString();

		QRectF card(option.rect);
		card.adjust(GUIHelper::scale(14.0), GUIHelper::scale(8.0), -GUIHelper::scale(14.0), -GUIHelper::scale(6.0));
		painter->setPen(QPen(QColor(t.border), 1));
		painter->setBrush(QColor(t.cardHover));
		painter->drawRoundedRect(card, 14.0, 14.0);

		// The magnifier rests in a pastel accent circle: lens ring plus a
		// short rounded handle, drawn with strokes (no glyph fonts, no icons).
		const qreal tileSide = GUIHelper::scale(30.0);
		const QRectF tile(card.center().x() - tileSide / 2.0, card.top() + GUIHelper::scale(12.0), tileSide, tileSide);
		painter->setPen(Qt::NoPen);
		painter->setBrush(withAlpha(QColor(t.accent), 38));
		painter->drawEllipse(tile);
		const QPointF lensCenter = tile.center() - QPointF(tileSide * 0.07, tileSide * 0.07);
		const qreal lensRadius = tileSide * 0.20;
		painter->setPen(QPen(QColor(t.accent), 2, Qt::SolidLine, Qt::RoundCap));
		painter->setBrush(Qt::NoBrush);
		painter->drawEllipse(lensCenter, lensRadius, lensRadius);
		const QPointF handleStart = lensCenter + QPointF(lensRadius * 0.75, lensRadius * 0.75);
		painter->drawLine(handleStart, handleStart + QPointF(tileSide * 0.16, tileSide * 0.16));

		QFont nameFont = option.font;
		nameFont.setWeight(QFont::DemiBold);
		const QFontMetricsF nameMetrics(nameFont);
		QFont captionFont = option.font;
		captionFont.setPointSizeF(option.font.pointSizeF() * 0.82);
		const QFontMetricsF captionMetrics(captionFont);

		const qreal textTop = tile.bottom() + GUIHelper::scale(8.0);
		painter->setFont(nameFont);
		painter->setPen(QColor(t.text));
		painter->drawText(QRectF(card.left(), textTop, card.width(), nameMetrics.height()),
			Qt::AlignHCenter | Qt::AlignVCenter, title);
		painter->setFont(captionFont);
		painter->setPen(QColor(t.mutedText));
		painter->drawText(QRectF(card.left(), textTop + nameMetrics.height() + GUIHelper::scale(2.0),
			card.width(), captionMetrics.height()),
			Qt::AlignHCenter | Qt::AlignVCenter, caption);
	}

	static void paintEntry(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
		const SkinTokens& t, bool dark, const QString& title)
	{
		QRectF row(option.rect);
		row.adjust(0, GUIHelper::scale(2.0), 0, -GUIHelper::scale(2.0));

		// The hovered row lifts one value step; the current row gets the
		// fully rounded stadium in the selection tint, the same silhouette as
		// the skin's chips. Calm: no fill change beyond one step, no glow.
		const bool selected = option.state.testFlag(QStyle::State_Selected);
		const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
		if (selected || hovered)
		{
			const qreal radius = row.height() / 2.0;
			if (selected)
			{
				painter->setPen(QPen(withAlpha(QColor(t.accent), dark ? 120 : 110), 1));
				painter->setBrush(QColor(t.cardSelected));
			}
			else
			{
				painter->setPen(Qt::NoPen);
				painter->setBrush(QColor(t.cardHover));
			}
			painter->drawRoundedRect(row.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
		}

		// A rounded-square colour tile carrying the entry's pictogram; the
		// monogram stays as the fallback for unmapped templates.
		const QColor tint = index.data(TintRole).value<QColor>();
		const qreal tileSide = GUIHelper::scale(28.0);
		const QRectF tile(row.left() + GUIHelper::scale(10.0), row.center().y() - tileSide / 2.0, tileSide, tileSide);
		painter->setPen(Qt::NoPen);
		painter->setBrush(tint);
		painter->drawRoundedRect(tile, tileSide * 0.32, tileSide * 0.32);
		const QString iconResource = index.data(IconRole).toString();
		if (!iconResource.isEmpty())
		{
			const int glyphSide = qMax(1, qRound(tileSide * 0.6));
			const QRect glyphRect(qRound(tile.center().x() - glyphSide / 2.0),
				qRound(tile.center().y() - glyphSide / 2.0), glyphSide, glyphSide);
			GUIHelper::tintedIcon(iconResource, QColor(QStringLiteral("#FAFAFC")), glyphSide)
				.paint(painter, glyphRect);
		}
		else
		{
			const QString glyph = index.data(GlyphRole).toString();
			QFont glyphFont = option.font;
			glyphFont.setWeight(QFont::Bold);
			glyphFont.setPointSizeF(option.font.pointSizeF() * (glyph.size() > 1 ? 0.9 : 1.1));
			painter->setFont(glyphFont);
			painter->setPen(QColor(QStringLiteral("#FAFAFC")));
			painter->drawText(tile, Qt::AlignCenter, glyph);
		}

		// Name over the config line as a friendly muted caption (regular
		// face, not monospace: here it is a description, not an editor).
		// The name is bold so it carries the row.
		const QString caption = index.data(CaptionRole).toString();
		const qreal textLeft = tile.right() + GUIHelper::scale(12.0);
		const qreal textWidth = row.right() - GUIHelper::scale(16.0) - textLeft;

		QFont nameFont = option.font;
		nameFont.setWeight(QFont::Bold);
		const QFontMetricsF nameMetrics(nameFont);
		QFont captionFont = option.font;
		captionFont.setPointSizeF(option.font.pointSizeF() * 0.82);
		const QFontMetricsF captionMetrics(captionFont);

		const qreal gap = caption.isEmpty() ? 0.0 : GUIHelper::scale(1.0);
		const qreal textHeight = nameMetrics.height() + gap + (caption.isEmpty() ? 0.0 : captionMetrics.height());
		const qreal textTop = row.center().y() - textHeight / 2.0;

		painter->setFont(nameFont);
		painter->setPen(QColor(t.text));
		painter->drawText(QRectF(textLeft, textTop, textWidth, nameMetrics.height()),
			Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(title, Qt::ElideRight, textWidth));

		if (!caption.isEmpty())
		{
			painter->setFont(captionFont);
			painter->setPen(QColor(t.mutedText));
			painter->drawText(QRectF(textLeft, textTop + nameMetrics.height() + gap, textWidth, captionMetrics.height()),
				Qt::AlignLeft | Qt::AlignVCenter, captionMetrics.elidedText(caption, Qt::ElideRight, textWidth));
		}
	}
};
}

SoftFilterPickerView::SoftFilterPickerView(const SkinTokens& tokens, QWidget* parent)
	: FilterPickerView(parent),
	  skinTokens(tokens)
{
	setObjectName(QStringLiteral("SoftFilterPicker"));

	QVBoxLayout* layout = new QVBoxLayout(this);
	const int pad = GUIHelper::scale(14.0);
	// The extra bottom margin keeps the list clear of the faked drop step
	// painted along the card's bottom edge (paintEvent).
	layout->setContentsMargins(pad, pad, pad, pad + GUIHelper::scale(2.0));
	layout->setSpacing(GUIHelper::scale(10.0));

	searchEdit = new QLineEdit(this);
	searchEdit->setObjectName(QStringLiteral("SoftPickerSearch"));
	searchEdit->setPlaceholderText(tr("Search filters"));
	searchEdit->setClearButtonEnabled(true);
	// Arrow keys and Return typed in the pill drive the list below, so
	// keyboard users never have to leave the field.
	layout->addWidget(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("SoftPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	listWidget->setUniformItemSizes(false);
	listWidget->setItemDelegate(new SoftPickerDelegate(skinTokens, listWidget));
	layout->addWidget(listWidget, 1);
	bindListPicker(searchEdit, listWidget, EntryIndexRole, [this]() { rebuildList(); });

	// Roomy and approachable: the widest, tallest-rowed picker of the five.
	setFixedWidth(GUIHelper::scale(380.0));
	setMaximumHeight(GUIHelper::scale(470.0));
}

void SoftFilterPickerView::entriesChanged()
{
	entryMonograms = softMonograms(pickerEntries());
	sectionColors.clear();
	const bool dark = skinTokens.dark;
	for (const FilterPickerEntry& entry : pickerEntries())
	{
		const QString section = entry.path.isEmpty() ? tr("General") : entry.path.join(QStringLiteral(" / "));
		if (!sectionColors.contains(section))
			sectionColors.insert(section, sectionPastel(sectionColors.size(), dark));
	}
	rebuildList();
	searchEdit->setFocus();
}

void SoftFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::PhaseAndTimeSearch)
	{
		// Where Delay and the two all-pass sections went. The category is
		// part of what the shared predicate searches, so one term returns
		// the whole group.
		searchEdit->setText(QStringLiteral("phase"));
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
	// The first selectable row is already the preselected stadium highlight;
	// parking the cursor there would photograph the selected style twice. The
	// cursor rests on the entry after it instead, so one frame shows both the
	// stadium selection and the one-value-step hover lift, each readable.
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

	// Hover is driven by real mouse events (the view keeps a hover index
	// updated from MouseMove); feed it a synthetic move over the target so
	// the offscreen render shows the hover styling.
	listWidget->viewport()->setAttribute(Qt::WA_UnderMouse, true);
	const QPointF center = listWidget->visualItemRect(target).center();
	QMouseEvent moveEvent(QEvent::MouseMove, center,
		listWidget->viewport()->mapToGlobal(center),
		Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(listWidget->viewport(), &moveEvent);
	listWidget->viewport()->update();
}

void SoftFilterPickerView::rebuildList()
{
	listWidget->clear();

	QString currentSection;
	bool sectionStarted = false;
	for (const FilterPickerMatch& match : pickerMatches())
	{
		const FilterPickerEntry& entry = pickerEntries()[match.originalIndex];
		const QString& section = match.section;

		const QColor tint = sectionColors.value(section,
			sectionPastel(0, skinTokens.dark));
		if (!sectionStarted || section != currentSection)
		{
			sectionStarted = true;
			currentSection = section;
			QListWidgetItem* caption = new QListWidgetItem(listWidget);
			caption->setFlags(Qt::NoItemFlags);
			caption->setData(EntryIndexRole, -1);
			caption->setData(TitleRole, section);
			caption->setData(TintRole, tint);
			caption->setData(KindRole, SectionItem);
		}

		QListWidgetItem* item = new QListWidgetItem(listWidget);
		item->setData(EntryIndexRole, match.originalIndex);
		item->setData(TitleRole, entry.name);
		// A calm sentence about what the filter does, not the raw config line.
		// The catalog describes every current template; softCaption stays the
		// fallback so a future, undescribed template still reads kindly.
		item->setData(CaptionRole,
			entry.description.isEmpty() ? softCaption(entry.line) : entry.description);
		item->setData(TintRole, tint);
		item->setData(KindRole, EntryItem);
		item->setData(GlyphRole, entryMonograms.value(match.originalIndex, entry.name.left(1).toUpper()));
		item->setData(IconRole, softEntryIcon(entry));
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

QSize SoftFilterPickerView::sizeHint() const
{
	const QMargins margins = layout()->contentsMargins();
	int height = margins.top() + margins.bottom() + layout()->spacing()
		+ searchEdit->sizeHint().height() + listContentHeight + GUIHelper::scale(4.0);
	height = qMin(height, maximumHeight());
	return QSize(GUIHelper::scale(380.0), height);
}

void SoftFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	const SkinTokens& t = skinTokens;
	const bool dark = skinTokens.dark;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// Backing for the rounded corners: the window background, which is also
	// what the popup floats over.
	painter.fillRect(rect(), QColor(t.background));

	// Faked elevation, per the constitution: one background value step nudged
	// down under the card; never a real shadow effect. The card rounds at the
	// constitutional 14px.
	const qreal radius = 14.0;
	QRectF card(rect());
	card.adjust(0.5, 0.5, -0.5, -2.5);
	const QColor stepColor = dark
		? mixColor(QColor(t.background), QColor(Qt::black), 0.45)
		: mixColor(QColor(t.background), QColor(t.border), 0.7);
	painter.setPen(Qt::NoPen);
	painter.setBrush(stepColor);
	painter.drawRoundedRect(card.translated(0, 2.0), radius, radius);

	// The menu card itself: rounded, one value step above the window, with
	// the very light 1px border.
	painter.setPen(QPen(QColor(t.border), 1));
	painter.setBrush(QColor(t.card));
	painter.drawRoundedRect(card, radius, radius);
}
