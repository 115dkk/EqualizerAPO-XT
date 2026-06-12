/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioFilterPicker.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Studio's glow rule: light is faked with layered strokes and gradients,
// never effects. These helpers mirror the alpha/mix vocabulary Skins.cpp
// uses for the card chrome so the picker speaks the exact same dialect.
QColor withAlpha(const QString& hex, int alpha)
{
	QColor color(hex);
	color.setAlpha(alpha);
	return color;
}

QColor mixColor(const QColor& a, const QColor& b, double t)
{
	return QColor(
		qRound(a.red() + (b.red() - a.red()) * t),
		qRound(a.green() + (b.green() - a.green()) * t),
		qRound(a.blue() + (b.blue() - a.blue()) * t));
}

// Item data roles. EntryIndexRole carries the ORIGINAL index into the
// entries list handed to setEntries; captions and notes carry -1.
constexpr int EntryIndexRole = Qt::UserRole;
constexpr int CaptionRole = Qt::UserRole + 1;
constexpr int FirstCaptionRole = Qt::UserRole + 2;
constexpr int EmptyNoteRole = Qt::UserRole + 3;

// A small magnifier glyph for the search field, drawn in token colours so
// both modes stay intentional without shipping an icon asset.
QPixmap makeSearchGlyph(const QColor& color)
{
	const int side = GUIHelper::scale(16.0);
	QPixmap pixmap(side, side);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	QPen pen(color, qMax(1.4, side / 10.0));
	pen.setCapStyle(Qt::RoundCap);
	painter.setPen(pen);
	const double radius = side * 0.28;
	const QPointF center(side * 0.42, side * 0.42);
	painter.drawEllipse(center, radius, radius);
	painter.drawLine(
		QPointF(center.x() + radius * 0.72, center.y() + radius * 0.72),
		QPointF(side * 0.82, side * 0.82));
	return pixmap;
}

// Paints captions as understated luminous dividers and entries as glass
// strips: hover pools light under the cursor (radial accent wash), the
// selected entry glows from within and carries the skin's signal lamp.
class StudioPickerDelegate : public QStyledItemDelegate
{
public:
	StudioPickerDelegate(const SkinTokens& tokens, bool dark, QObject* parent)
		: QStyledItemDelegate(parent), t(tokens), dark(dark)
	{
	}

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		Q_UNUSED(option);
		if (index.data(CaptionRole).toBool())
			return QSize(0, GUIHelper::scale(index.data(FirstCaptionRole).toBool() ? 22.0 : 31.0));
		if (index.data(EmptyNoteRole).toBool())
			return QSize(0, GUIHelper::scale(44.0));
		return QSize(0, GUIHelper::scale(30.0));
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);

		if (index.data(CaptionRole).toBool())
			paintCaption(painter, option, index);
		else if (index.data(EmptyNoteRole).toBool())
			paintEmptyNote(painter, option, index);
		else
			paintEntry(painter, option, index);

		painter->restore();
	}

private:
	void paintCaption(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
	{
		const int pad = GUIHelper::scale(10.0);
		// The extra height above non-first captions is the section gap; the
		// caption text itself sits in the lower band.
		QRectF rect = QRectF(option.rect).adjusted(pad, 0, -pad, 0);
		rect.setTop(rect.bottom() - GUIHelper::scale(20.0));

		QFont font(t.fontFamily);
		font.setPointSizeF(7.6);
		font.setWeight(QFont::DemiBold);
		font.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
		painter->setFont(font);
		painter->setPen(withAlpha(t.mutedText, dark ? 215 : 235));

		const QString text = index.data(Qt::DisplayRole).toString().toUpper();
		painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);

		// Slightly luminous divider: a hairline that picks up the accent near
		// the caption and dissolves toward the right edge.
		const double textWidth = QFontMetricsF(font).horizontalAdvance(text);
		const double y = rect.center().y() + 0.5;
		const double x0 = rect.left() + textWidth + GUIHelper::scale(8.0);
		if (x0 < rect.right() - GUIHelper::scale(12.0))
		{
			QLinearGradient line(x0, y, rect.right(), y);
			line.setColorAt(0.0, withAlpha(t.accent, dark ? 110 : 130));
			line.setColorAt(0.55, withAlpha(t.accent2, dark ? 45 : 60));
			line.setColorAt(1.0, withAlpha(t.accent2, 0));
			painter->setPen(QPen(QBrush(line), 1.0));
			painter->drawLine(QPointF(x0, y), QPointF(rect.right(), y));
		}
	}

	void paintEmptyNote(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
	{
		QFont font(t.fontFamily);
		font.setPointSizeF(9.0);
		font.setItalic(true);
		painter->setFont(font);
		painter->setPen(withAlpha(t.mutedText, 200));
		painter->drawText(option.rect, Qt::AlignCenter, index.data(Qt::DisplayRole).toString());
	}

	void paintEntry(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
	{
		const bool selected = option.state.testFlag(QStyle::State_Selected);
		const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
		const QRectF rect = QRectF(option.rect).adjusted(2.0, 1.0, -2.0, -1.0);
		const double radius = 8.0;

		QPainterPath path;
		path.addRoundedRect(rect, radius, radius);

		if (selected)
		{
			// Glass selection lit from within: gradient fill, a wide soft
			// inner stroke as the pooled light, a crisp 1px edge, and the
			// skin's signal lamp on the left rim.
			QLinearGradient fill(rect.topLeft(), rect.bottomLeft());
			fill.setColorAt(0.0, withAlpha(t.accent, dark ? 70 : 52));
			fill.setColorAt(1.0, withAlpha(t.accent, dark ? 32 : 24));
			painter->fillPath(path, fill);

			painter->setBrush(Qt::NoBrush);
			painter->setPen(QPen(withAlpha(t.accent, dark ? 52 : 40), 4.0));
			painter->drawRoundedRect(rect.adjusted(2.0, 2.0, -2.0, -2.0), radius - 2.0, radius - 2.0);
			painter->setPen(QPen(withAlpha(t.accent, dark ? 200 : 180), 1.0));
			painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

			// Signal lamp: the same vertical fade-in/out segment the studio
			// cards wear, with its bloom.
			const double segment = qMin(16.0, rect.height() - 8.0);
			const double y0 = rect.center().y() - segment / 2.0;
			QLinearGradient bloom(0, y0 - 3.0, 0, y0 + segment + 3.0);
			bloom.setColorAt(0.0, withAlpha(t.accent, 0));
			bloom.setColorAt(0.5, withAlpha(t.accent, 80));
			bloom.setColorAt(1.0, withAlpha(t.accent, 0));
			QLinearGradient lamp(0, y0, 0, y0 + segment);
			lamp.setColorAt(0.0, withAlpha(t.accent, 0));
			lamp.setColorAt(0.5, withAlpha(t.accent, 245));
			lamp.setColorAt(1.0, withAlpha(t.accent, 0));
			painter->save();
			painter->setClipPath(path);
			painter->fillRect(QRectF(rect.left(), y0 - 3.0, 4.0, segment + 6.0), bloom);
			painter->fillRect(QRectF(rect.left() + 1.0, y0, 2.0, segment), lamp);
			painter->restore();
		}
		else if (hovered)
		{
			// Light pooling under the cursor: a faint sheen plus a radial
			// accent wash that brightens the middle of the strip.
			painter->fillPath(path, QColor(255, 255, 255, dark ? 8 : 90));
			QRadialGradient pool(rect.center(), rect.width() * 0.46);
			pool.setColorAt(0.0, withAlpha(t.accent, dark ? 40 : 30));
			pool.setColorAt(1.0, withAlpha(t.accent, 0));
			painter->fillPath(path, pool);
		}

		QFont font(t.fontFamily);
		font.setPointSizeF(9.4);
		if (selected)
			font.setWeight(QFont::DemiBold);
		painter->setFont(font);
		QColor textColor(t.text);
		if (!selected && !hovered)
			textColor = mixColor(QColor(t.text), QColor(t.mutedText), 0.18);
		painter->setPen(textColor);
		painter->drawText(rect.adjusted(GUIHelper::scale(12.0), 0, -GUIHelper::scale(8.0), 0),
			Qt::AlignLeft | Qt::AlignVCenter,
			painter->fontMetrics().elidedText(
				index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
				qRound(rect.width() - GUIHelper::scale(20.0))));
	}

	SkinTokens t;
	bool dark;
};
}

StudioFilterPickerView::StudioFilterPickerView(QWidget* parent)
	: FilterPickerView(parent)
{
	setObjectName(QStringLiteral("StudioFilterPicker"));
	skinTokens = SkinManager::instance()->tokens();
	// The hooks convention: studio's dark background is near-black, so
	// luminance is an unambiguous mode proxy (see Skins.cpp).
	dark = QColor(skinTokens.background).lightness() < 128;

	const int glow = GUIHelper::scale(13.0);
	const int pad = GUIHelper::scale(12.0);
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(glow + pad, glow + pad, glow + pad, glow + pad);
	layout->setSpacing(GUIHelper::scale(10.0));

	// Prominent sunken-glass search field: the inverse of the raised panel,
	// with a darker top edge as the inner shadow (the skin's input rule).
	searchEdit = new QLineEdit(this);
	searchEdit->setObjectName(QStringLiteral("StudioPickerSearch"));
	searchEdit->setPlaceholderText(tr("Search filters"));
	searchEdit->setClearButtonEnabled(true);
	searchEdit->addAction(QIcon(makeSearchGlyph(QColor(skinTokens.mutedText))), QLineEdit::LeadingPosition);
	const QString sunken = dark ? skinTokens.surfaceSunken : skinTokens.graph;
	const QString focusBackground = dark ? skinTokens.surface : skinTokens.card;
	const QString innerShadow = dark
		? QStringLiteral("rgba(0, 0, 0, 0.55)") : QStringLiteral("rgba(24, 32, 51, 0.16)");
	searchEdit->setStyleSheet(QStringLiteral(
		"QLineEdit#StudioPickerSearch {"
		" background: %1; color: %2;"
		" border: 1px solid %3; border-top-color: %4;"
		" border-radius: 9px; padding: 6px 10px 6px 4px;"
		" selection-background-color: %5; selection-color: #f8fafc;"
		" font-family: \"%6\"; font-size: 10pt; }"
		"QLineEdit#StudioPickerSearch:focus {"
		" border: 1px solid %5; background: %7; }")
		.arg(sunken, skinTokens.text, skinTokens.border, innerShadow,
			skinTokens.accent, skinTokens.fontFamily, focusBackground));
	// Arrow keys and Return typed in the search field drive the list below,
	// so keyboard users never have to leave the field.
	searchEdit->installEventFilter(this);
	connect(searchEdit, &QLineEdit::textChanged, this, &StudioFilterPickerView::rebuildList);
	layout->addWidget(searchEdit);
	setFocusProxy(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("StudioPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	listWidget->setMouseTracking(true);
	listWidget->setItemDelegate(new StudioPickerDelegate(skinTokens, dark, listWidget));
	// The scroll bar gap is filled with the global QAbstractScrollArea
	// background (the deep stage colour), which would cut a hard stripe into
	// the glass. Restyle it as an intentional sunken-glass channel whose
	// opaque track sits on the panel's own colour family.
	const QColor track = mixColor(QColor(skinTokens.card), QColor(skinTokens.background), dark ? 0.45 : 0.40);
	const QString handleColor = dark
		? QStringLiteral("rgba(255, 255, 255, 0.16)") : QStringLiteral("rgba(24, 32, 51, 0.22)");
	listWidget->setStyleSheet(QStringLiteral(
		"QListWidget#StudioPickerList { background: transparent; border: 0; outline: 0; }"
		"QListWidget#StudioPickerList::item { border: 0; padding: 0; }"
		"QListWidget#StudioPickerList QScrollBar:vertical {"
		" background: %1; width: 8px; margin: 0; border: 0; border-radius: 4px; }"
		"QListWidget#StudioPickerList QScrollBar::handle:vertical {"
		" background: %2; min-height: 32px; border-radius: 4px; }"
		"QListWidget#StudioPickerList QScrollBar::handle:vertical:hover {"
		" background: %3; }"
		"QListWidget#StudioPickerList QScrollBar::handle:vertical:pressed {"
		" background: %4; }"
		"QListWidget#StudioPickerList QScrollBar::add-line:vertical,"
		"QListWidget#StudioPickerList QScrollBar::sub-line:vertical {"
		" background: transparent; border: 0; width: 0; height: 0; }"
		"QListWidget#StudioPickerList QScrollBar::add-page:vertical,"
		"QListWidget#StudioPickerList QScrollBar::sub-page:vertical {"
		" background: transparent; }")
		.arg(QStringLiteral("rgb(%1, %2, %3)").arg(track.red()).arg(track.green()).arg(track.blue()),
			handleColor,
			QStringLiteral("rgba(%1, %2, %3, 0.55)")
			.arg(QColor(skinTokens.accent).red()).arg(QColor(skinTokens.accent).green()).arg(QColor(skinTokens.accent).blue()),
			skinTokens.accent));
	listWidget->setMinimumHeight(GUIHelper::scale(330.0));
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

	setMinimumWidth(GUIHelper::scale(388.0));
	setMaximumWidth(GUIHelper::scale(440.0));
	setMaximumHeight(GUIHelper::scale(470.0));
}

void StudioFilterPickerView::setEntries(const QList<FilterPickerEntry>& entries)
{
	allEntries = entries;
	rebuildList();
	searchEdit->setFocus();
}

void StudioFilterPickerView::rebuildList()
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

		if (!sectionStarted || section != currentSection)
		{
			QListWidgetItem* caption = new QListWidgetItem(section, listWidget);
			caption->setFlags(Qt::NoItemFlags);
			caption->setData(EntryIndexRole, -1);
			caption->setData(CaptionRole, true);
			caption->setData(FirstCaptionRole, !sectionStarted);
			sectionStarted = true;
			currentSection = section;
		}

		QListWidgetItem* item = new QListWidgetItem(entry.name, listWidget);
		item->setData(EntryIndexRole, i);
		item->setToolTip(entry.line);
	}

	if (listWidget->count() == 0)
	{
		QListWidgetItem* note = new QListWidgetItem(tr("No matching filters"), listWidget);
		note->setFlags(Qt::NoItemFlags);
		note->setData(EntryIndexRole, -1);
		note->setData(EmptyNoteRole, true);
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
}

void StudioFilterPickerView::chooseCurrent()
{
	QListWidgetItem* item = listWidget->currentItem();
	if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
		emit entryChosen(item->data(EntryIndexRole).toInt());
}

bool StudioFilterPickerView::eventFilter(QObject* watched, QEvent* event)
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

void StudioFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// The stage: an opaque strip of the deep studio background around the
	// panel. The popup host is a plain opaque window, so the glass effect is
	// staged here; over the editor (whose background is this same token) the
	// margin melts away and only the lit panel appears to float.
	const QColor background(skinTokens.background);
	painter.fillRect(rect(), background);

	const double glow = GUIHelper::scale(13.0);
	const QRectF panel = QRectF(rect()).adjusted(glow, glow, -glow, -glow);
	const double radius = GUIHelper::scale(14.0);
	const QColor accent(skinTokens.accent);
	const QColor card(skinTokens.card);

	// Soft elevation shadow: stacked translucent rounded rects drifting
	// downward (no effects, per the skin's glow rule).
	painter.setPen(Qt::NoPen);
	for (int i = 4; i >= 1; i--)
	{
		painter.setBrush(QColor(0, 0, 0, dark ? 26 - i * 4 : 24 - i * 4));
		painter.drawRoundedRect(panel.translated(0, i + 1).adjusted(-i, -i, i, i), radius + i, radius + i);
	}

	// Light behind the glass: the accent glow hugging the border, painted as
	// progressively tighter, brighter strokes.
	painter.setBrush(Qt::NoBrush);
	const struct { double width; int alpha; } glowLayers[] = {
		{ 11.0, dark ? 14 : 16 },
		{ 7.0, dark ? 26 : 30 },
		{ 4.0, dark ? 44 : 48 },
		{ 2.0, dark ? 66 : 70 }
	};
	for (const auto& layer : glowLayers)
	{
		QColor stroke = accent;
		stroke.setAlpha(layer.alpha);
		painter.setPen(QPen(stroke, layer.width));
		painter.drawRoundedRect(panel, radius, radius);
	}

	// The glass itself: a vertical gradient slab over the deep background.
	QPainterPath panelPath;
	panelPath.addRoundedRect(panel, radius, radius);
	QLinearGradient glass(panel.topLeft(), panel.bottomLeft());
	if (dark)
	{
		glass.setColorAt(0.0, mixColor(card, QColor(255, 255, 255), 0.05));
		glass.setColorAt(1.0, mixColor(card, background, 0.30));
	}
	else
	{
		glass.setColorAt(0.0, card);
		glass.setColorAt(1.0, mixColor(card, background, 0.35));
	}
	painter.setPen(Qt::NoPen);
	painter.fillPath(panelPath, glass);

	// The light sources behind the glass: a key light shining through near
	// the search field and a violet rim light low in the opposite corner.
	// They tint the panel from within, which is what makes it read as lit
	// glass rather than a flat card.
	QRadialGradient keyLight(
		QPointF(panel.center().x() - panel.width() * 0.16, panel.top() + panel.height() * 0.08),
		panel.width() * 0.62);
	keyLight.setColorAt(0.0, withAlpha(skinTokens.accent, dark ? 38 : 30));
	keyLight.setColorAt(1.0, withAlpha(skinTokens.accent, 0));
	painter.fillPath(panelPath, keyLight);
	QRadialGradient rimLight(
		QPointF(panel.right() - panel.width() * 0.10, panel.bottom() - panel.height() * 0.06),
		panel.width() * 0.55);
	rimLight.setColorAt(0.0, withAlpha(skinTokens.accent2, dark ? 30 : 22));
	rimLight.setColorAt(1.0, withAlpha(skinTokens.accent2, 0));
	painter.fillPath(panelPath, rimLight);

	// Frost sheen across the upper region: the light caught in the glass.
	QLinearGradient sheen(panel.topLeft(), QPointF(panel.left(), panel.top() + panel.height() * 0.42));
	sheen.setColorAt(0.0, QColor(255, 255, 255, dark ? 20 : 150));
	sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
	painter.fillPath(panelPath, sheen);

	// 1px border: an icy reflection at the top dissolving into accent light
	// toward the bottom.
	QLinearGradient edge(panel.topLeft(), panel.bottomLeft());
	if (dark)
	{
		edge.setColorAt(0.0, QColor(255, 255, 255, 56));
		edge.setColorAt(0.45, withAlpha(skinTokens.border, 230));
		edge.setColorAt(1.0, withAlpha(skinTokens.accent, 90));
	}
	else
	{
		edge.setColorAt(0.0, QColor(255, 255, 255, 240));
		edge.setColorAt(0.45, withAlpha(skinTokens.border, 255));
		edge.setColorAt(1.0, withAlpha(skinTokens.accent, 110));
	}
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QBrush(edge), 1.0));
	painter.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

	// The luminous arc: an accent-to-violet light caught on the top edge,
	// echoing the skin's glowing knob arcs. Bloom first, then the core.
	const double arcSpan = panel.width() * 0.58;
	const double arcX0 = panel.center().x() - arcSpan / 2.0;
	const double arcY = panel.top();
	QLinearGradient arc(arcX0, arcY, arcX0 + arcSpan, arcY);
	arc.setColorAt(0.0, withAlpha(skinTokens.accent, 0));
	arc.setColorAt(0.35, withAlpha(skinTokens.accent, dark ? 255 : 235));
	arc.setColorAt(0.7, withAlpha(skinTokens.accent2, dark ? 220 : 195));
	arc.setColorAt(1.0, withAlpha(skinTokens.accent2, 0));
	QLinearGradient arcBloom(arcX0, arcY, arcX0 + arcSpan, arcY);
	arcBloom.setColorAt(0.0, withAlpha(skinTokens.accent, 0));
	arcBloom.setColorAt(0.35, withAlpha(skinTokens.accent, dark ? 95 : 75));
	arcBloom.setColorAt(0.7, withAlpha(skinTokens.accent2, dark ? 75 : 58));
	arcBloom.setColorAt(1.0, withAlpha(skinTokens.accent2, 0));
	QPen bloomPen(QBrush(arcBloom), 5.0);
	bloomPen.setCapStyle(Qt::RoundCap);
	painter.setPen(bloomPen);
	painter.drawLine(QPointF(arcX0, arcY), QPointF(arcX0 + arcSpan, arcY));
	QPen arcPen(QBrush(arc), 2.0);
	arcPen.setCapStyle(Qt::RoundCap);
	painter.setPen(arcPen);
	painter.drawLine(QPointF(arcX0, arcY), QPointF(arcX0 + arcSpan, arcY));
}
