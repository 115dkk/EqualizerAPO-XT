/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftFilterPicker.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpression>
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
	// The template's config line (entries only).
	CaptionRole,
	// The category pastel (QColor).
	TintRole,
	// SoftPickerItemKind.
	KindRole
};

enum SoftPickerItemKind
{
	EntryItem = 0,
	SectionItem,
	EmptyStateItem
};

// Linear blend between two colours, the skin's stand-in for shadows and tints
// (Soft fakes all elevation by mixing token colours; see Skins.cpp).
QColor mixColor(const QColor& a, const QColor& b, double t)
{
	return QColor(
		qRound(a.red() + (b.red() - a.red()) * t),
		qRound(a.green() + (b.green() - a.green()) * t),
		qRound(a.blue() + (b.blue() - a.blue()) * t));
}

QColor withAlpha(QColor color, int alpha)
{
	color.setAlpha(alpha);
	return color;
}

// Friendly pastel hues handed out to categories in catalog order, the way a
// consumer settings app gives every group its own icon colour.
QColor sectionPastel(int sectionIndex, bool dark)
{
	static const int hues[] = { 216, 150, 26, 268, 336, 190, 48, 0, 286, 120 };
	const int hue = hues[sectionIndex % int(sizeof(hues) / sizeof(hues[0]))];
	return QColor::fromHslF(hue / 360.0, dark ? 0.52 : 0.58, dark ? 0.64 : 0.56);
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
			return QSize(GUIHelper::scale(100.0), GUIHelper::scale(64.0));
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
			painter->setPen(QColor(t.mutedText));
			painter->drawText(option.rect, Qt::AlignCenter, title);
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
		// square colour tile carrying the entry's initial.
		const QColor tint = index.data(TintRole).value<QColor>();
		const qreal tileSide = GUIHelper::scale(28.0);
		const QRectF tile(row.left() + GUIHelper::scale(10.0), row.center().y() - tileSide / 2.0, tileSide, tileSide);
		painter->setPen(Qt::NoPen);
		painter->setBrush(tint);
		painter->drawRoundedRect(tile, tileSide * 0.32, tileSide * 0.32);
		QFont glyphFont = option.font;
		glyphFont.setWeight(QFont::DemiBold);
		glyphFont.setPointSizeF(option.font.pointSizeF() * 1.1);
		painter->setFont(glyphFont);
		painter->setPen(QColor(QStringLiteral("#FAFAFC")));
		painter->drawText(tile, Qt::AlignCenter, title.left(1).toUpper());

		// Name over the config line as a friendly muted caption (regular
		// face, not monospace: here it is a description, not an editor).
		const QString caption = index.data(CaptionRole).toString();
		const qreal textLeft = tile.right() + GUIHelper::scale(12.0);
		const qreal textWidth = row.right() - GUIHelper::scale(16.0) - textLeft;

		QFont nameFont = option.font;
		nameFont.setWeight(QFont::DemiBold);
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
		item->setData(CaptionRole, entry.line);
		item->setData(TintRole, tint);
		item->setData(KindRole, EntryItem);
		item->setToolTip(entry.line);
	}

	// A friendly, quiet empty state instead of a bare void.
	if (listWidget->count() == 0)
	{
		QListWidgetItem* empty = new QListWidgetItem(listWidget);
		empty->setFlags(Qt::NoItemFlags);
		empty->setData(EntryIndexRole, -1);
		empty->setData(TitleRole, tr("Nothing matches your search"));
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
	// down under the card; never a real shadow effect.
	const qreal radius = 12.0;
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
