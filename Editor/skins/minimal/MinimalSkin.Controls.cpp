/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

// "The number is the control; the knob is confirmation." The figure is the
// brightest ink in the row - painted here when the widget supplies
// valueText, living in the adjacent ValueScrubBox (promoted by
// precision_*.qss) for the row dials, which supply none. The knob itself
// is a hairline instrument: a 1px 270-degree range arc, a travelled arc in
// text ink and a radial cursor tick at the value angle - no filled disc,
// no hub. Monochrome until dragged; dragging turns the travelled ink
// accent (active-state law).
void MinimalSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor hairline(tokens.border);
	const QColor secondary(tokens.mutedText);
	const QColor active(tokens.accent);
	// The promoted figure sits one brightness step above body text: white on
	// the dark console, full black on the light paper. Mode is read off the
	// background's value because SkinTokens carries no dark flag.
	const bool darkMode = skinIsDark(tokens);
	const QColor promoted(!state.enabled ? secondary
		: (darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0)));
	const QColor travelled = !state.enabled ? secondary
		: (state.dragging ? active : QColor(tokens.text));

	const bool hasNumber = !state.valueText.isEmpty();
	const double arcRadius = hasNumber ? 9.0 : 12.0;

	QFont numberFont(tokens.monoFontFamily);
	numberFont.setBold(true);
	numberFont.setPointSizeF(9.0);

	QPointF arcCenter;
	QRectF numberRect;
	if (hasNumber)
	{
		// Number left (primary), confirmation arc beside it; the pair is
		// centred in the widget. Shrink the font instead of clipping when a
		// long value (e.g. "-100.0") meets a narrow widget.
		const double gap = 6.0;
		double available = rect.width() - 2.0 * arcRadius - gap - 4.0;
		double textWidth = QFontMetricsF(numberFont).horizontalAdvance(state.valueText);
		while (textWidth > available && numberFont.pointSizeF() > 6.5)
		{
			numberFont.setPointSizeF(numberFont.pointSizeF() - 0.5);
			textWidth = QFontMetricsF(numberFont).horizontalAdvance(state.valueText);
		}
		const double pairWidth = textWidth + gap + 2.0 * arcRadius;
		const double left = rect.left() + (rect.width() - pairWidth) / 2.0;
		numberRect = QRectF(left, rect.top(), textWidth, rect.height());
		arcCenter = QPointF(left + textWidth + gap + arcRadius, QRectF(rect).center().y());
	}
	else
	{
		// Arc only; keep a constant bottom strip free for the hover/drag
		// readout so the instrument does not jump when the readout appears.
		arcCenter = QPointF(QRectF(rect).center().x(), rect.top() + (rect.height() - 14.0) / 2.0);
	}

	// Hairline range arc: the full 270-degree travel, 1px, open across the
	// bottom dead zone like every knob in the product.
	const QRectF arcRect(arcCenter.x() - arcRadius, arcCenter.y() - arcRadius, arcRadius * 2.0, arcRadius * 2.0);
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(arcRect, -135 * 16, -270 * 16);

	if (state.bipolar)
	{
		// Fixed detent tick at 12 o'clock and a 1px deviation arc measured
		// from it: boost grows clockwise, cut counter-clockwise. On the
		// detent the deviation vanishes and only the tick remains - the
		// honest "0 dB".
		painter.setPen(QPen(secondary, 1));
		painter.drawLine(skinArcPoint(arcCenter, arcRadius - 2.5, -270.0),
			skinArcPoint(arcCenter, arcRadius + 2.5, -270.0));
		const double deviationDegrees = 270.0 * (state.ratio - 0.5);
		painter.setPen(QPen(travelled, 1));
		painter.drawArc(arcRect, -270 * 16, -qRound(deviationDegrees * 16.0));
	}
	else
	{
		// Unipolar: the travelled range fills from the arc's start. No detent
		// tick, no centre origin - the two kinds cannot be confused.
		painter.setPen(QPen(travelled, 1));
		painter.drawArc(arcRect, -135 * 16, -qRound(270.0 * state.ratio * 16.0));
	}

	// Radial cursor tick crossing the range arc at the value angle.
	const double valueDegrees = -(135.0 + 270.0 * state.ratio);
	painter.setPen(QPen(travelled, 1));
	painter.drawLine(skinArcPoint(arcCenter, arcRadius - 3.0, valueDegrees),
		skinArcPoint(arcCenter, arcRadius + 3.0, valueDegrees));

	if (hasNumber)
	{
		painter.setFont(numberFont);
		painter.setPen((state.enabled && state.dragging) ? active : promoted);
		painter.drawText(numberRect, Qt::AlignVCenter | Qt::AlignLeft, state.valueText);
	}
	else if (state.enabled && (state.hovered || state.dragging))
	{
		// No supplied value text: show the dial position derived from ratio.
		// The real value sits in the adjacent scrub box, so a percentage is
		// the only honest readout for log-scaled legacy dials.
		QFont readoutFont(tokens.monoFontFamily);
		readoutFont.setPointSizeF(7.5);
		painter.setFont(readoutFont);
		painter.setPen(state.dragging ? active : secondary);
		const QRectF readoutRect(rect.left(), rect.bottom() - 14.0, rect.width(), 14.0);
		painter.drawText(readoutRect, Qt::AlignCenter, QStringLiteral("%1%").arg(qRound(state.ratio * 100.0)));
	}

	// Keyboard focus: a square hairline frame (radius 0 corner language).
	if (state.focused)
	{
		painter.setPen(QPen(QColor(tokens.focusRing), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
	}
}

// A row of mutually exclusive choices as this skin's selector field.
//
// The constitution already answers "pick one of a few": not a box you open,
// but a caption wearing a caret over a single hairline underline (X5). A
// segmented control is that same field with every choice printed at once, so
// the underline stops being the field's decoration and becomes the mark that
// says which word the field currently holds. No pill, no filled cell, no glow
// - a fill would say "selected" in a sheet where a fill already says
// "pressed", and a terminal has no third value to spend.
//
// One device per state, which is what keeps a monochrome control readable at
// roughly 76x24: the travelling rule is the value, one background value step
// (with the ink lifting to body brightness) is hover, the reverse block is the
// press, the square accent hairline is focus, and the ink ladder alone carries
// disabled. Every one of them is separable at a glance from the others.
void MinimalSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty())
		return;

	// Rules and blocks are crisp on the pixel grid; only the type is
	// antialiased. Same law as the GraphicEQ plot.
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRect rect = state.rect;
	const QColor bodyInk(tokens.text);
	const QColor secondary(tokens.mutedText);
	const QColor inverted(tokens.surface);

	// The field's ground is the card step the other fields on the analysis bar
	// stand on (stat chips, combos), so the control reads as one more field on
	// the same printed form rather than a widget dropped onto it. Disabled
	// sinks exactly one step below it.
	painter.fillRect(rect, QColor(state.enabled ? tokens.card : tokens.surface));

	QStringList words;
	words.reserve(state.labels.size());
	for (const QString& label : state.labels)
		words.append(label.toUpper());
	const int count = int(words.size());

	// Uppercase tracked mono, one size for every cell (hierarchy is brightness
	// and weight here, never size). A cramped cell shrinks the type the way the
	// knob's figure does - never clipped - and only elides at the floor.
	QFont font(tokens.monoFontFamily);
	font.setPointSizeF(9.0);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	const double cellWidth = state.segmentRect(0).width();
	const auto widestWord = [&words](const QFont& probe) {
		// Measured bold: the chosen word is the widest one the cell must hold.
		QFont heavy(probe);
		heavy.setBold(true);
		const QFontMetricsF metrics(heavy);
		double widest = 0.0;
		for (const QString& word : words)
			widest = qMax(widest, metrics.horizontalAdvance(word));
		return widest;
	};
	while (widestWord(font) > cellWidth - 10.0 && font.pointSizeF() > 6.5)
		font.setPointSizeF(font.pointSizeF() - 0.5);

	// The mark's rule sits under the type and clear of the field's bottom edge:
	// an underlined word inside a slot, not a second frame line.
	const int markY = rect.bottom() - qBound(3, rect.height() / 5, 6);
	const QRectF typeBand(rect.left(), rect.top(), rect.width(), markY - rect.top());

	for (int i = 0; i < count; i++)
	{
		const QRectF cell = state.segmentRect(i);
		const bool pressedCell = state.enabled && state.pressedIndex == i;
		const bool hoveredCell = state.enabled && state.hoveredIndex == i && !pressedCell;

		// Cells are snapped to whole pixels before anything is filled, so a
		// fractional cell edge never leaves a soft seam between two of them.
		const QRect fill = QRect(qRound(cell.left()), rect.top() + 1,
			qRound(cell.right()) - qRound(cell.left()), rect.height() - 2)
			.intersected(rect.adjusted(1, 1, -1, -1));

		QColor ink(secondary);
		if (pressedCell)
		{
			// The engraved-command press: the cell swaps ink and ground. The
			// choice is a word, so the swap holds (reverse video keeps the
			// glyph) - the same block the add row and the toast close print.
			painter.fillRect(fill, bodyInk);
			ink = inverted;
		}
		else if (hoveredCell)
		{
			// Exactly one background value step, and the caption ink lifts to
			// body brightness because the cell acts on click (the add row's
			// hover law). The step alone is a couple of values on the dark
			// console; the ink lift is what makes the hover arrive.
			painter.fillRect(fill, QColor(tokens.cardHover));
			ink = bodyInk;
		}
		else if (state.enabled && i == state.selectedIndex)
		{
			ink = bodyInk;
		}

		QFont cellFont(font);
		cellFont.setBold(i == state.selectedIndex);
		painter.setFont(cellFont);
		painter.setPen(ink);
		// Absolute tracking adds a step after the last glyph too, so a centred
		// advance sits half a step right of true centre. Alignment is this
		// skin's whole argument, so the half step is taken back.
		const QRectF wordCell(cell.left() - 0.5, typeBand.top(), cell.width(), typeBand.height());
		painter.drawText(wordCell, Qt::AlignCenter,
			QFontMetricsF(cellFont).elidedText(words.at(i), Qt::ElideRight, cell.width() - 4.0));
	}

	// The mark reads selectionPosition, not selectedIndex, and sizes itself to
	// the word it is under: running through three choices is one cursor moving
	// between fields (stretching as it goes from MAG to PHASE), not three cells
	// lighting up in turn. Accent stays out of it - a chosen metric is a value,
	// not a live gesture, and the accent belongs to focus.
	const double position = qBound(0.0, state.selectionPosition, double(count - 1));
	const int low = qBound(0, qFloor(position), count - 1);
	const int high = qMin(low + 1, count - 1);
	const double travel = position - low;
	QFont markFont(font);
	markFont.setBold(true);
	const QFontMetricsF markMetrics(markFont);
	const double markWidth = qMin(cellWidth - 4.0,
		markMetrics.horizontalAdvance(words.at(low)) * (1.0 - travel)
		+ markMetrics.horizontalAdvance(words.at(high)) * travel + 6.0);
	const double markCenter = state.segmentRect(position).center().x();
	// Pressing the cell the mark is already under inverts the whole cell, so
	// the rule inverts with the word it belongs to.
	const bool markInverted = state.enabled && state.pressedIndex >= 0
		&& state.pressedIndex == state.selectedIndex;
	painter.setPen(QPen(!state.enabled ? secondary : (markInverted ? inverted : bodyInk), 1));
	painter.drawLine(qRound(markCenter - markWidth / 2.0), markY,
		qRound(markCenter + markWidth / 2.0), markY);

	// The field's edge: one square 1px hairline. Keyboard focus swaps it for
	// the accent hairline, the frame this skin puts around every focused
	// control; the mark keeps body ink underneath so the armed field still
	// says which cell it holds.
	painter.setPen(QPen(QColor(state.focused && state.enabled ? tokens.focusRing : tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(rect.adjusted(0, 0, -1, -1));
}
