/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftFilterPicker.h"
#include "Editor/skins/SkinPaint.h"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
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

// withAlpha lives in the shared SkinPaint.h.

// Friendly pastel hues handed out to categories in catalog order, the way a
// consumer settings app gives every group its own icon colour.
QColor sectionPastel(int sectionIndex, bool dark)
{
	static const int hues[] = { 216, 150, 26, 268, 336, 190, 48, 0, 286, 120 };
	const int hue = hues[sectionIndex % int(sizeof(hues) / sizeof(hues[0]))];
	return QColor::fromHslF(hue / 360.0, dark ? 0.52 : 0.58, dark ? 0.64 : 0.56);
}

// AR1 F4: single initials collided across the catalog (Comment, Channel,
// Copy and Convolution all wore "C"), so every tile now carries a per-item
// monogram. Multi-word names take their first two word initials ("Low-pass
// filter" -> "LP"), single-word names their first two letters ("Channel" ->
// "Ch"), and a catalog-wide uniqueness pass walks the remaining letters of a
// single-word name whose candidate is taken (Comment keeps "Co", Copy
// becomes "Cp", Convolution "Cn"). Deterministic in catalog order, the same
// cosmetic trade the section hues already make; the category pastel stays
// the second disambiguator.
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
// shape carries its own response-curve glyph. Pictures read friendlier than
// the two-letter monograms (user direction, AR2 rework round); an unmapped
// template returns empty and the tile falls back to its AR1 F4 monogram, so
// future catalog entries degrade gracefully instead of going blank.
QString softEntryIcon(const FilterPickerEntry& entry)
{
	const QString line = entry.line.trimmed();
	if (line.startsWith(QLatin1Char('#')))
		return QStringLiteral(":/icons/modern/comment-bubble.svg");

	const int colon = line.indexOf(QLatin1Char(':'));
	const QString command = colon > 0 ? line.left(colon).trimmed() : QString();
	if (command == QLatin1String("Filter"))
	{
		static const struct { const char* token; const char* icon; } curves[] = {
			{ " PK ", "eq-peaking" },
			{ " LP ", "eq-lowpass" },
			{ " HP ", "eq-highpass" },
			{ " BP ", "eq-bandpass" },
			{ " LS ", "eq-lowshelf" },
			{ " HS ", "eq-highshelf" },
			{ " NO ", "eq-notch" },
			{ " AP ", "eq-allpass" }
		};
		for (const auto& curve : curves)
			if (line.contains(QLatin1String(curve.token)))
				return QStringLiteral(":/icons/modern/%1.svg").arg(QLatin1String(curve.icon));
		return QStringLiteral(":/icons/modern/eq-peaking.svg");
	}

	static const struct { const char* command; const char* icon; } commands[] = {
		{ "Include", "file-include" },
		{ "Convolution", "waveform" },
		{ "MultiConvolution", "multi-convolution" },
		{ "VSTPlugin", "plugin" },
		{ "GraphicEQ", "graphic-eq" },
		// The volume ramp, not a knob: Soft's audience reads consumer
		// volume glyphs, and the knob face read as a clock (user direction).
		{ "Preamp", "preamp-gain" },
		{ "Delay", "delay-clock" },
		{ "Device", "device-speaker" },
		// A channel list with a check: the command SELECTS the channels the
		// following filters apply to - a fork glyph read as splitting the
		// signal (user direction).
		{ "Channel", "channel-select" },
		{ "Stage", "stage-chain" },
		{ "Copy", "route-channels" },
		{ "LoudnessCorrection", "loudness" }
	};
	for (const auto& mapping : commands)
		if (command == QLatin1String(mapping.command))
			return QStringLiteral(":/icons/modern/%1.svg").arg(QLatin1String(mapping.icon));
	return QString();
}

// AR1 F5: templates that insert a bare command ("Include:", "Copy: ", "# ")
// used to print that fragment as the caption, which read as a truncated
// line. The display layer swaps empty and colon-ended previews for a calm
// promise; the raw line keeps living in the tooltip and in what the choice
// actually inserts, so nothing is hidden, only phrased kindly.
QString softCaption(const QString& line)
{
	const QString display = line.trimmed();
	if (display.isEmpty() || display.endsWith(QLatin1Char(':')) || display == QStringLiteral("#"))
		return QCoreApplication::translate("SoftFilterPickerView", "Choose the details after adding");
	return display;
}

// Paints the menu rows: stadium highlights, rounded-square colour tiles and
// pill section headers, all from the live skin tokens so both modes stay calm.
class SoftPickerDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

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
		const SkinTokens& t = SkinManager::instance()->tokens();
		const bool dark = SkinManager::instance()->isDark();
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

	// AR1 X6: the fruitless search is a moment, not a void. A friendly card
	// one value step above the menu surface holds a pastel circle with a
	// painted magnifier, the title in full ink and a caption in the muted
	// face - the same calm grammar as every other soft surface, no warning
	// colour anywhere.
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

		// The category announces itself like an iOS Settings row: a rounded
		// square colour tile carrying the entry's pictogram (pictures over
		// monograms - user direction, AR2 rework round; the AR1 F4 monogram
		// stays as the fallback for unmapped templates).
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
		// The name is bold (AR1 F4) so it carries the row now that the tile
		// shares its letters with the whole catalog.
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

SoftFilterPickerView::SoftFilterPickerView(QWidget* parent)
	: FilterPickerView(parent)
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
	searchEdit->installEventFilter(this);
	connect(searchEdit, &QLineEdit::textChanged, this, &SoftFilterPickerView::rebuildList);
	layout->addWidget(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("SoftPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	listWidget->setUniformItemSizes(false);
	listWidget->setItemDelegate(new SoftPickerDelegate(listWidget));
	// Dropdown semantics: one click inserts.
	connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
		if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
			emit entryChosen(item->data(EntryIndexRole).toInt());
	});
	connect(listWidget, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
			emit entryChosen(item->data(EntryIndexRole).toInt());
	});
	layout->addWidget(listWidget, 1);

	// Roomy and approachable: the widest, tallest-rowed picker of the five.
	setFixedWidth(GUIHelper::scale(380.0));
	setMaximumHeight(GUIHelper::scale(470.0));
}

void SoftFilterPickerView::setEntries(const QList<FilterPickerEntry>& entries)
{
	allEntries = entries;
	entryMonograms = softMonograms(entries);
	sectionColors.clear();
	const bool dark = SkinManager::instance()->isDark();
	for (const FilterPickerEntry& entry : entries)
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

	const QStringList terms = searchEdit->text().split(
		QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

	QString currentSection;
	bool sectionStarted = false;
	for (int i = 0; i < allEntries.size(); i++)
	{
		const FilterPickerEntry& entry = allEntries[i];
		const QString section = entry.path.isEmpty() ? tr("General") : entry.path.join(QStringLiteral(" / "));

		bool matches = true;
		const QString haystack = section + QLatin1Char(' ') + entry.name + QLatin1Char(' ') + entry.line;
		for (const QString& term : terms)
		{
			if (!haystack.contains(term, Qt::CaseInsensitive))
			{
				matches = false;
				break;
			}
		}
		if (!matches)
			continue;

		const QColor tint = sectionColors.value(section,
			sectionPastel(0, SkinManager::instance()->isDark()));
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
		item->setData(EntryIndexRole, i);
		item->setData(TitleRole, entry.name);
		item->setData(CaptionRole, softCaption(entry.line));
		item->setData(TintRole, tint);
		item->setData(KindRole, EntryItem);
		item->setData(GlyphRole, entryMonograms.value(i, entry.name.left(1).toUpper()));
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
	for (int row = 0; row < listWidget->count(); row++)
	{
		if (listWidget->item(row)->flags() & Qt::ItemIsSelectable)
		{
			listWidget->setCurrentRow(row);
			break;
		}
	}

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

void SoftFilterPickerView::chooseCurrent()
{
	QListWidgetItem* item = listWidget->currentItem();
	if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
		emit entryChosen(item->data(EntryIndexRole).toInt());
}

bool SoftFilterPickerView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == searchEdit && event->type() == QEvent::KeyPress)
	{
		QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
		switch (keyEvent->key())
		{
		case Qt::Key_Down:
		case Qt::Key_Up:
		case Qt::Key_PageDown:
		case Qt::Key_PageUp:
			QApplication::sendEvent(listWidget, event);
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			chooseCurrent();
			return true;
		default:
			break;
		}
	}
	return FilterPickerView::eventFilter(watched, event);
}

void SoftFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	const SkinTokens& t = SkinManager::instance()->tokens();
	const bool dark = SkinManager::instance()->isDark();

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// Backing for the rounded corners: the window background, which is also
	// what the popup floats over.
	painter.fillRect(rect(), QColor(t.background));

	// Faked elevation, per the constitution: one background value step nudged
	// down under the card; never a real shadow effect. The card rounds at the
	// constitutional 14px (AR1 F2).
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
