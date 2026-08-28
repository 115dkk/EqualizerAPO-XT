/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See RackFilterPicker.h. The small screw/LED painters are deliberately
	local expressions of Rack's hardware idiom; they stay visually
	consistent with the card chrome.
*/

#include "RackFilterPicker.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
enum
{
	// Original index into the entries list handed to setEntries.
	OriginalIndexRole = Qt::UserRole,
	// Row kind: section plate or labeled slot.
	KindRole = Qt::UserRole + 1
};

enum RowKind
{
	EntryRow = 0,
	SectionRow = 1
};

// Engraved faceplate printing: a contrast pass offset one pixel down (the
// recess edge catching the light), then the body color on top.
void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

// A slotted machine screw, same construction as the card faceplates'.
void paintScrew(QPainter& painter, const QPointF& center, qreal radius, qreal slotDegrees, bool dark)
{
	QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35), radius * 2.1);
	if (dark)
	{
		body.setColorAt(0.0, QColor(0x9A, 0xA4, 0xAC));
		body.setColorAt(0.55, QColor(0x4E, 0x57, 0x5E));
		body.setColorAt(1.0, QColor(0x23, 0x28, 0x2C));
	}
	else
	{
		body.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		body.setColorAt(0.55, QColor(0xC4, 0xBD, 0xAE));
		body.setColorAt(1.0, QColor(0x8E, 0x86, 0x76));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : QColor(0x6B, 0x62, 0x52), 1));
	painter.setBrush(body);
	painter.drawEllipse(center, radius, radius);

	const qreal rad = qDegreesToRadians(slotDegrees);
	const QPointF dir(qCos(rad), qSin(rad));
	const QPointF a = center - dir * (radius - 1.2);
	const QPointF b = center + dir * (radius - 1.2);
	painter.setPen(QPen(dark ? QColor(10, 12, 14, 230) : QColor(60, 54, 44, 220), 1.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a, b);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 60 : 170), 0.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a + QPointF(0, 1), b + QPointF(0, 1));
}

// A panel LED in a bezel ring; glow scales 0..1 so the hover lamp can sit
// between fully dark and fully lit. Unlit lamps recede one step (thinner
// bezel ink, translucent dome, fainter specular dot) so a column of module
// slots never reads as bullet spam.
void paintLed(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, qreal glow, bool dark)
{
	const bool unlit = glow <= 0.0;
	painter.setPen(QPen(dark ? QColor(0, 0, 0, unlit ? 110 : 190) : QColor(70, 62, 50, unlit ? 100 : 190), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (glow > 0.0)
	{
		QRadialGradient halo(center, radius * 3.2);
		halo.setColorAt(0.0, withAlpha(litColor, int(110 * glow)));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, radius * 3.2, radius * 3.2);
	}

	QRadialGradient dome(center - QPointF(radius * 0.3, radius * 0.3), radius * 1.6);
	const QColor off = litColor.darker(330);
	const QColor hot = litColor.lighter(150);
	auto mix = [glow](const QColor& a, const QColor& b) {
		return QColor(
			qRound(a.red() + (b.red() - a.red()) * glow),
			qRound(a.green() + (b.green() - a.green()) * glow),
			qRound(a.blue() + (b.blue() - a.blue()) * glow));
	};
	QColor domeTop = mix(off.lighter(140), hot);
	QColor domeEdge = mix(off, litColor.darker(125));
	if (unlit)
	{
		domeTop.setAlpha(140);
		domeEdge.setAlpha(140);
	}
	dome.setColorAt(0.0, domeTop);
	dome.setColorAt(1.0, domeEdge);
	painter.setPen(Qt::NoPen);
	painter.setBrush(dome);
	painter.drawEllipse(center, radius, radius);
	painter.setBrush(QColor(255, 255, 255, unlit ? (dark ? 14 : 30)
		: int((dark ? 28 : 60) + (170 - (dark ? 28 : 60)) * glow)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

// Paints section plates and labeled slots; the panel behind them belongs to
// RackFilterPickerView::paintEvent.
class RackFilterPickerDelegate : public QStyledItemDelegate
{
public:
	RackFilterPickerDelegate(const SkinTokens& tokens, QObject* parent)
		: QStyledItemDelegate(parent), skinTokens(tokens)
	{
	}

	const SkinTokens skinTokens;

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		Q_UNUSED(option);
		const bool section = index.data(KindRole).toInt() == SectionRow;
		return QSize(0, GUIHelper::scale(section ? 24.0 : 26.0));
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		const SkinTokens& tokens = skinTokens;
		const bool dark = skinIsDark(tokens);

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);

		if (index.data(KindRole).toInt() == SectionRow)
			paintSection(painter, option, index, tokens, dark);
		else
			paintEntry(painter, option, index, tokens, dark);

		painter->restore();
	}

private:
	// An engraved section plate riveted onto the panel: a slightly recessed
	// band, two rivets, uppercase letter-spaced printing.
	void paintSection(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const SkinTokens& tokens, bool dark) const
	{
		const QRectF plate = QRectF(option.rect).adjusted(2, 5, -2, -3);

		painter->setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
		painter->setBrush(QColor(0, 0, 0, dark ? 52 : 16));
		painter->drawRoundedRect(plate, 2, 2);
		// The lower plate edge catches the work light.
		painter->setPen(QPen(QColor(255, 255, 255, dark ? 26 : 140), 1));
		painter->drawLine(QPointF(plate.left() + 2, plate.bottom() + 1), QPointF(plate.right() - 2, plate.bottom() + 1));

		// Rivets at both plate ends, like the VST brass nameplate's.
		painter->setPen(QPen(dark ? QColor(0, 0, 0, 180) : QColor(0x6B, 0x62, 0x52), 0.8));
		painter->setBrush(dark ? QColor(0x6A, 0x74, 0x7C) : QColor(0xD8, 0xCF, 0xBC));
		painter->drawEllipse(QPointF(plate.left() + 7, plate.center().y()), 1.6, 1.6);
		painter->drawEllipse(QPointF(plate.right() - 7, plate.center().y()), 1.6, 1.6);

		QFont plateFont(tokens.fontFamily);
		plateFont.setPixelSize(9);
		plateFont.setBold(true);
		plateFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
		painter->setFont(plateFont);
		const QRectF textRect = plate.adjusted(14, 0, -14, 0);
		engraveText(*painter, textRect, Qt::AlignVCenter | Qt::AlignLeft,
			index.data(Qt::DisplayRole).toString().toUpper(),
			withAlpha(QColor(tokens.mutedText), 220), dark);
	}

	// A labeled slot: panel LED left of the printed label. Selection lights
	// the LED amber and backlights the slot; hover is a faint lamp glow.
	void paintEntry(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const SkinTokens& tokens, bool dark) const
	{
		const bool selected = option.state & QStyle::State_Selected;
		const bool hovered = option.state & QStyle::State_MouseOver;
		const QRectF slot = QRectF(option.rect).adjusted(2, 1, -2, -1);
		const QColor accent(tokens.accent);

		if (selected)
		{
			// Backlit slot: the lamp sits behind the LED end, so the light
			// falls off toward the right.
			QLinearGradient backlight(slot.topLeft(), slot.topRight());
			backlight.setColorAt(0.0, withAlpha(accent, dark ? 64 : 70));
			backlight.setColorAt(1.0, withAlpha(accent, dark ? 12 : 16));
			painter->setPen(QPen(withAlpha(accent, 130), 1));
			painter->setBrush(backlight);
			painter->drawRoundedRect(slot, 2, 2);
		}
		else if (hovered)
		{
			QLinearGradient lamp(slot.topLeft(), slot.topRight());
			lamp.setColorAt(0.0, withAlpha(accent, dark ? 44 : 48));
			lamp.setColorAt(1.0, withAlpha(accent, 0));
			painter->setPen(Qt::NoPen);
			painter->setBrush(lamp);
			painter->drawRoundedRect(slot, 2, 2);
		}

		const QPointF led(slot.left() + 11.0, slot.center().y());
		paintLed(*painter, led, 2.8, accent, selected ? 1.0 : (hovered ? 0.55 : 0.0), dark);

		QFont labelFont(tokens.fontFamily);
		labelFont.setPixelSize(12);
		painter->setFont(labelFont);
		const QRectF textRect = slot.adjusted(24, 0, -8, 0);
		const QString label = QFontMetrics(labelFont).elidedText(
			index.data(Qt::DisplayRole).toString(), Qt::ElideRight, int(textRect.width()));
		painter->setPen(withAlpha(QColor(tokens.text), selected ? 255 : 225));
		painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
	}
};
}

RackFilterPickerView::RackFilterPickerView(const SkinTokens& tokens, QWidget* parent)
	: FilterPickerView(parent),
	  skinTokens(tokens)
{
	setObjectName(QStringLiteral("RackFilterPicker"));

	const bool dark = skinIsDark(tokens);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(GUIHelper::scale(12.0), GUIHelper::scale(38.0), GUIHelper::scale(12.0), GUIHelper::scale(20.0));
	layout->setSpacing(GUIHelper::scale(8.0));

	// The search strip is an LCD character display set into the faceplate:
	// a dark well with green segments in both modes (hardware displays do
	// not follow the panel finish).
	searchEdit = new QLineEdit(this);
	searchEdit->setObjectName(QStringLiteral("RackFilterPickerSearch"));
	searchEdit->setPlaceholderText(tr("SEARCH"));
	const QColor lcdInk = dark ? QColor(0x86, 0xF2, 0xBA) : QColor(0x3E, 0xD6, 0x8E);
	QFont lcdFont(tokens.monoFontFamily);
	lcdFont.setPointSizeF(10.0);
	lcdFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	searchEdit->setFont(lcdFont);
	searchEdit->setStyleSheet(QStringLiteral(
		"QLineEdit#RackFilterPickerSearch {"
		" background: #0A0E0C; color: %1;"
		" border: 1px solid #050706; border-bottom-color: %2;"
		" border-radius: 2px; padding: 4px 8px;"
		" selection-background-color: %1; selection-color: #0A0E0C; }"
		"QLineEdit#RackFilterPickerSearch:focus { border: 1px solid %3; }")
		.arg(lcdInk.name(), dark ? QStringLiteral("#39424A") : QStringLiteral("#FFFFFF"), tokens.accent));
	QPalette lcdPalette = searchEdit->palette();
	lcdPalette.setColor(QPalette::PlaceholderText, withAlpha(lcdInk, 110));
	searchEdit->setPalette(lcdPalette);
	layout->addWidget(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("RackFilterPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setUniformItemSizes(false);
	// The delegate paints slots straight onto the brushed panel; the global
	// item-view styling (well background, hover bands) must not interfere.
	listWidget->setStyleSheet(QStringLiteral(
		"QListWidget#RackFilterPickerList { background: transparent; border: 0; }"
		"QListWidget#RackFilterPickerList::item { background: transparent; padding: 0; border: 0; }"));
	listWidget->viewport()->setMouseTracking(true);
	listWidget->setItemDelegate(new RackFilterPickerDelegate(skinTokens, listWidget));
	layout->addWidget(listWidget, 1);
	bindListPicker(searchEdit, listWidget, OriginalIndexRole, [this]() { rebuildList(); });

	// The host gives focus to the view; the LCD is where typing belongs.
	setFocusProxy(searchEdit);
	setMinimumWidth(GUIHelper::scale(360.0));
	setMaximumHeight(GUIHelper::scale(480.0));
}

void RackFilterPickerView::entriesChanged()
{
	rebuildList();
	searchEdit->setFocus();
}

void RackFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::PhaseAndTimeSearch)
	{
		// Where Delay and the two all-pass sections went. The category is
		// part of what the shared predicate searches, so one term returns
		// the whole group.
		searchEdit->setText(QStringLiteral("PHASE"));
		return;
	}
	if (kind == GalleryShowcase::EmptySearch)
	{
		// A query no module matches: the LCD keeps the dead search term and
		// the faceplate engraves NO SIGNAL (the constitution's empty-result
		// law) where the slots would be.
		searchEdit->setText(QStringLiteral("ZZZZ"));
		return;
	}

	// HoverFirstEntry: warm a lamp without stealing the selection shot. The
	// first selectable slot is already selected (lit amber), so the lamp
	// pre-heat is staged on the next slot - the capture then shows both
	// states of the light grammar at once. Hover is driven by real mouse
	// events, so feed the viewport a synthetic move.
	searchEdit->clear();
	int selectableSeen = 0;
	for (int row = 0; row < listWidget->count(); row++)
	{
		QListWidgetItem* item = listWidget->item(row);
		if (!(item->flags() & Qt::ItemIsSelectable))
			continue;
		if (++selectableSeen < 2)
			continue;
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

QSize RackFilterPickerView::sizeHint() const
{
	// As tall as the module's slots require, up to the rack height limit;
	// past that the slot column scrolls behind the faceplate.
	const QMargins margins = layout()->contentsMargins();
	const int height = margins.top() + searchEdit->sizeHint().height()
		+ layout()->spacing() + listContentHeight + margins.bottom();
	return QSize(GUIHelper::scale(366.0), qMin(height, GUIHelper::scale(470.0)));
}

void RackFilterPickerView::rebuildList()
{
	listWidget->clear();

	int sectionCount = 0;
	int entryCount = 0;
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
			QListWidgetItem* plate = new QListWidgetItem(section, listWidget);
			plate->setFlags(Qt::NoItemFlags);
			plate->setData(KindRole, int(SectionRow));
			sectionCount++;
		}

		QListWidgetItem* item = new QListWidgetItem(entry.name, listWidget);
		item->setData(OriginalIndexRole, match.originalIndex);
		item->setData(KindRole, int(EntryRow));
		item->setToolTip(entry.line);
		entryCount++;
	}

	listContentHeight = sectionCount * GUIHelper::scale(24.0)
		+ entryCount * GUIHelper::scale(26.0) + GUIHelper::scale(4.0);
	updateGeometry();

	// Preselect the first real slot so Return inserts immediately.
	selectFirstListEntry();
}

void RackFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	const SkinTokens& tokens = skinTokens;
	const bool dark = skinIsDark(tokens);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = tokens.borderRadius;

	// Brushed faceplate: the card metal with a rolled top edge falling into
	// shadow, exactly the card chrome's sheen direction.
	const QColor plateColor(tokens.card);
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, plateColor.lighter(138));
		sheen.setColorAt(0.12, plateColor.lighter(112));
		sheen.setColorAt(0.55, plateColor);
		sheen.setColorAt(1.0, plateColor.darker(130));
	}
	else
	{
		sheen.setColorAt(0.0, plateColor.lighter(104));
		sheen.setColorAt(0.5, plateColor);
		sheen.setColorAt(1.0, plateColor.darker(108));
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(sheen);
	painter.drawRoundedRect(r, radius, radius);

	// Horizontal brushing grain (same construction as the card faceplates'):
	// per-line ink variation with sparse polish lines, in logical coordinates
	// so DPI scales the grain.
	{
		const uint seed = uint(qHash(QStringLiteral("module-select-brush")));
		const int baseAlpha = 5;
		QColor grain = dark ? QColor(255, 255, 255) : QColor(96, 84, 64);
		for (qreal y = r.top() + 2; y < r.bottom() - 1; y += 2)
		{
			const uint h = (seed ^ uint(qRound(y * 7.0))) * 2654435761u;
			const bool polish = (h >> 8) % 11u == 0;
			grain.setAlpha(baseAlpha + int(h % 7u) + (polish ? 6 : 0));
			painter.setPen(QPen(grain, 1));
			painter.drawLine(QPointF(r.left() + 2, y), QPointF(r.right() - 2, y));
		}
	}

	// Machined plate edge: dark outline, lit top bezel, shadowed bottom.
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 210) : QColor(0x8A, 0x80, 0x6C), 1));
	painter.drawRoundedRect(r, radius, radius);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left() + radius, r.top() + 1), QPointF(r.right() - radius, r.top() + 1));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 140 : 60), 1));
	painter.drawLine(QPointF(r.left() + radius, r.bottom() - 1), QPointF(r.right() - radius, r.bottom() - 1));

	// Four corner screws; fixed slot angles so the faceplate looks hand-set,
	// not stamped.
	paintScrew(painter, QPointF(r.left() + 11, r.top() + 11), 3.6, 23.0, dark);
	paintScrew(painter, QPointF(r.right() - 11, r.top() + 11), 3.6, 117.0, dark);
	paintScrew(painter, QPointF(r.left() + 11, r.bottom() - 11), 3.6, 64.0, dark);
	paintScrew(painter, QPointF(r.right() - 11, r.bottom() - 11), 3.6, 158.0, dark);

	// Engraved header: the unit designation, with the power LED on the right.
	QFont titleFont(tokens.fontFamily);
	titleFont.setPixelSize(10);
	titleFont.setBold(true);
	titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
	painter.setFont(titleFont);
	const QRectF titleRect(r.left() + 26, r.top() + 6, r.width() - 80, 22);
	engraveText(painter, titleRect, Qt::AlignVCenter | Qt::AlignLeft,
		QStringLiteral("MODULE SELECT"), withAlpha(QColor(tokens.mutedText), 230), dark);
	paintLed(painter, QPointF(r.right() - 28, r.top() + 17), 3.0, QColor(tokens.accent2), 1.0, dark);

	// Machined groove separating the header from the controls.
	const qreal grooveY = r.top() + 31;
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
	painter.drawLine(QPointF(r.left() + 8, grooveY), QPointF(r.right() - 8, grooveY));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 130), 1));
	painter.drawLine(QPointF(r.left() + 8, grooveY + 1), QPointF(r.right() - 8, grooveY + 1));

	// A filtered-out catalog: engrave NO SIGNAL where the slots would be.
	if (listWidget != nullptr && listWidget->count() == 0)
	{
		QFont emptyFont(tokens.fontFamily);
		emptyFont.setPixelSize(10);
		emptyFont.setBold(true);
		emptyFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
		painter.setFont(emptyFont);
		engraveText(painter, QRectF(listWidget->geometry()), Qt::AlignCenter,
			QStringLiteral("NO SIGNAL"), withAlpha(QColor(tokens.mutedText), 170), dark);
	}

	// Tiny model engraving on the bottom rail, between the screws.
	QFont modelFont(tokens.fontFamily);
	modelFont.setPixelSize(8);
	modelFont.setBold(true);
	modelFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
	painter.setFont(modelFont);
	engraveText(painter, QRectF(r.left() + 26, r.bottom() - 16, r.width() - 52, 12),
		Qt::AlignVCenter | Qt::AlignHCenter, QStringLiteral("EAPO-XT SERIES"),
		withAlpha(QColor(tokens.mutedText), 140), dark);
}
