/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/KnobTravel.h"

namespace
{
// Ink ladder shared by the drum's parts: hairline (rim lines, the travel
// rule), secondary (crown lines, window rules, the detent), travelled ink
// (reading line and position tick: body text, accent while dragging,
// secondary when disabled), promoted figure (one step above body text:
// white on the dark console, full black on the light paper; mode is read
// off the background's value because SkinTokens carries no dark flag).
struct KnobInk
{
	QColor hairline;
	QColor secondary;
	QColor active;
	QColor promoted;
	QColor travelled;
};

KnobInk knobInk(const KnobState& state, const SkinTokens& tokens)
{
	KnobInk ink;
	ink.hairline = QColor(tokens.border);
	ink.secondary = QColor(tokens.mutedText);
	ink.active = QColor(tokens.accent);
	const bool darkMode = skinIsDark(tokens);
	ink.promoted = !state.enabled ? ink.secondary
		: (darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0));
	ink.travelled = !state.enabled ? ink.secondary
		: (state.dragging ? ink.active : QColor(tokens.text));
	return ink;
}

// Bold mono figure, shrunk (never clipped) until it fits the width (AR1 N2).
QFont knobNumberFont(const SkinTokens& tokens, const QString& text, double available)
{
	QFont font(tokens.monoFontFamily);
	font.setBold(true);
	font.setPointSizeF(10.0);
	while (QFontMetricsF(font).horizontalAdvance(text) > available && font.pointSizeF() > 6.5)
		font.setPointSizeF(font.pointSizeF() - 0.5);
	return font;
}

// A cylinder seen edge-on with a horizontal axis: the surface line at angle
// theta (degrees; 0 faces the viewer) projects to y = cy - R sin(theta) and
// is visible while cos(theta) > 0. Rolling the drum shifts every theta by
// the same phase, so straight hairlines alone read as a turning wheel: open
// at the crown, dense toward the rims, appearing at one rim and vanishing
// at the other.
struct DrumLine
{
	int y;
	bool crown;   // near the crown (faces the viewer) vs. compressed at the rim
};

QList<DrumLine> drumLines(int cy, int radius, double phaseDegrees, int perRevolution)
{
	QList<DrumLine> lines;
	for (int k = 0; k < perRevolution; k++)
	{
		const double theta = qDegreesToRadians(k * 360.0 / perRevolution + phaseDegrees);
		const double facing = qCos(theta);
		if (facing <= 0.05)
			continue;
		lines.append({ cy - qRound(radius * qSin(theta)), facing > 0.55 });
	}
	return lines;
}

// Drum rotation across the whole range. The surface covers exactly the
// pointer travel that sweeps the range (KnobTravel::RangePixels, the figure
// AudioKnob's VerticalDrag gesture uses), so under a drag the drum rolls
// with the pointer one to one: 200px of travel is 200px of surface, about
// one and a half turns of a 22px drum. Value up rolls the surface up.
double drumTravelDegrees(int radius)
{
	return KnobTravel::RangePixels * 360.0 / (2.0 * M_PI * radius);
}

// Keyboard focus: a square hairline frame (radius 0 corner language).
void paintKnobFocus(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	if (!state.focused)
		return;
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(QColor(tokens.focusRing), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
}

// Dials without a figure (their number lives in the ValueScrubBox) show the
// dial position as a percentage in the constant bottom strip while hovered
// or dragged: the only honest readout for a log-scaled dial.
void paintKnobReadout(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens, const KnobInk& ink)
{
	if (!state.enabled || !(state.hovered || state.dragging))
		return;
	QFont readoutFont(tokens.monoFontFamily);
	readoutFont.setPointSizeF(8.5);
	painter.setFont(readoutFont);
	painter.setPen(state.dragging ? ink.active : ink.secondary);
	const QRectF readoutRect(rect.left(), rect.bottom() - 14.0, rect.width(), 14.0);
	painter.drawText(readoutRect, Qt::AlignCenter, QStringLiteral("%1%").arg(qRound(state.ratio * 100.0)));
}
}

// The register drum. "The number is the control; the knob is confirmation"
// still holds - the figure is the brightest ink in the row - but the knob
// is now seen edge-on, the way an adding machine shows its register:
// straight hairlines spaced as a cylinder's surface roll with the value,
// the figure is printed on the drum inside a two-rule window, an index tick
// against each rim is the reading line, and a hairline travel rule on the
// right carries the position in range (bipolar drums mark the detent on
// it). Dials without a figure are the bare drum. Under 64px the travel rule
// folds so the window keeps the figure at full size (a crowded row squeezes
// the knob to 50px). Monochrome until dragged; dragging turns the reading
// line, the position tick and the figure accent (active-state law). The
// drum is rolled, not turned: knobGesture() asks AudioKnob for the vertical
// drag, and the rotation per range is derived from that gesture's travel so
// the surface moves with the pointer.
void MinimalSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	const KnobInk ink = knobInk(state, tokens);
	const bool hasNumber = !state.valueText.isEmpty();
	const bool travelRule = rect.width() >= 64;
	const int radius = 22;
	const int left = rect.left() + 5;
	const int right = rect.right() - (travelRule ? 13 : 5);
	const int cy = hasNumber ? rect.center().y() : rect.top() + (rect.height() - 14) / 2;
	const int windowHalf = hasNumber ? 11 : 0;

	// Rims, then the surface (skipped behind the window).
	painter.setPen(QPen(ink.hairline, 1));
	painter.drawLine(left, cy - radius, left, cy + radius);
	painter.drawLine(right, cy - radius, right, cy + radius);
	for (const DrumLine& line : drumLines(cy, radius, state.ratio * drumTravelDegrees(radius), 24))
	{
		if (qAbs(line.y - cy) <= windowHalf)
			continue;
		painter.setPen(QPen(line.crown ? ink.secondary : ink.hairline, 1));
		painter.drawLine(left + 1, line.y, right - 1, line.y);
	}
	if (hasNumber)
	{
		painter.setPen(QPen(ink.secondary, 1));
		painter.drawLine(left, cy - windowHalf, right, cy - windowHalf);
		painter.drawLine(left, cy + windowHalf, right, cy + windowHalf);
	}

	// Reading line: an index tick against each rim.
	painter.setPen(QPen(ink.travelled, 1));
	painter.drawLine(left - 4, cy, left - 1, cy);
	painter.drawLine(right + 1, cy, right + 4, cy);

	if (travelRule)
	{
		const int ruleX = right + 9;
		painter.setPen(QPen(ink.hairline, 1));
		painter.drawLine(ruleX, cy - radius, ruleX, cy + radius);
		if (state.bipolar)
		{
			painter.setPen(QPen(ink.secondary, 1));
			painter.drawLine(ruleX - 2, cy, ruleX + 2, cy);
		}
		const int positionY = cy + radius - qRound(state.ratio * 2.0 * radius);
		painter.setPen(QPen(ink.travelled, 1));
		painter.drawLine(ruleX - 2, positionY, ruleX + 2, positionY);
	}

	if (hasNumber)
	{
		painter.setFont(knobNumberFont(tokens, state.valueText, right - left - 4.0));
		painter.setPen((state.enabled && state.dragging) ? ink.active : ink.promoted);
		painter.drawText(QRectF(left, cy - windowHalf, right - left, 2.0 * windowHalf), Qt::AlignCenter, state.valueText);
	}
	else
	{
		paintKnobReadout(painter, rect, state, tokens, ink);
	}
	paintKnobFocus(painter, rect, state, tokens);
}

KnobGesture MinimalSkin::knobGesture() const
{
	// A drum is rolled, not turned: the pointer's vertical travel moves the
	// surface under it.
	return KnobGesture::VerticalDrag;
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
	font.setPointSizeF(10.0);
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

// The VST3 bus contract folded into the terminal line: a lowercase caption
// ("in"/"out"), the value as bright ink wearing the X5 selector grammar - a
// caret and a 1px underline that exists only under the cursor or focus,
// accent only while open - and ASCII throughout. No cell, no fill; the line
// is the instrument.
void MinimalSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF rect(state.rect);
	QFont roleFont(tokens.monoFontFamily);
	roleFont.setPixelSize(10);
	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(12);

	QColor roleInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	QColor valueInk(state.enabled ? tokens.text : tokens.mutedText);

	const QString role = state.roleToken.toLower();
	painter.setFont(roleFont);
	painter.setPen(roleInk);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(role);
	painter.drawText(QRectF(rect.left() + 3.0, rect.top(), roleWidth, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, role);

	QString value = state.layoutText;
	if (state.channelCount > 0)
		value += QStringLiteral(":%1").arg(state.channelCount);
	painter.setFont(valueFont);
	painter.setPen(valueInk);
	const qreal valueLeft = rect.left() + 3.0 + roleWidth + 5.0;
	const qreal valueWidth = QFontMetricsF(valueFont).horizontalAdvance(value);
	painter.drawText(QRectF(valueLeft, rect.top(), rect.width() - (valueLeft - rect.left()), rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, value);

	// The caret the X5 caption wears; painted, so no font decides its shape.
	const qreal caretLeft = valueLeft + valueWidth + 4.0;
	const qreal caretHalf = 2.5;
	const qreal caretMidY = rect.center().y() + 1.0;
	QColor caretInk = state.menuOpen || state.focused ? QColor(tokens.accent) : roleInk;
	painter.setRenderHint(QPainter::Antialiasing, true);
	QPainterPath caret;
	caret.moveTo(caretLeft, caretMidY - caretHalf / 2.0);
	caret.lineTo(caretLeft + caretHalf * 2.0, caretMidY - caretHalf / 2.0);
	caret.lineTo(caretLeft + caretHalf, caretMidY + caretHalf);
	caret.closeSubpath();
	painter.fillPath(caret, caretInk);
	painter.setRenderHint(QPainter::Antialiasing, false);

	// The 1px underline: absent at rest, hairline under the cursor, accent
	// while focused or open (state as ink, not chrome).
	if (state.enabled && (state.hovered || state.focused || state.menuOpen))
	{
		const QColor line = state.focused || state.menuOpen
			? QColor(tokens.accent) : QColor(tokens.mutedText);
		painter.setPen(QPen(line, 1));
		const int underlineY = qRound(rect.bottom() - 3.0);
		painter.drawLine(QPointF(valueLeft, underlineY), QPointF(valueLeft + valueWidth, underlineY));
	}
}

// The joint is the terminal's ASCII "->", and the verdict is a printed tag
// on the severity ink ladder ("ok" for an engaged contract - untranslated
// like every terminal token). No lamps: state is ink on this instrument.
void MinimalSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	QFont monoFont(tokens.monoFontFamily);
	monoFont.setPixelSize(11);
	painter.setFont(monoFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	painter.drawText(QRectF(state.jointRect), Qt::AlignCenter, QStringLiteral("->"));

	const bool pairVerdict = !state.verdictInputText.isEmpty() || !state.verdictOutputText.isEmpty();
	const bool hasText = pairVerdict || !state.verdictText.isEmpty();
	if (state.verdictRect.isEmpty()
		|| (!hasText && state.tone == VstBusFrameState::Tone::Neutral))
		return;

	QColor ink(tokens.mutedText);
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success: ink = QColor(tokens.success); break;
	case VstBusFrameState::Tone::Warning: ink = QColor(tokens.warning); break;
	case VstBusFrameState::Tone::Critical: ink = QColor(tokens.danger); break;
	case VstBusFrameState::Tone::Neutral: break;
	}
	if (!state.enabled)
		ink = withAlpha(ink, 150);

	QFont verdictFont(tokens.monoFontFamily);
	verdictFont.setPixelSize(10);
	painter.setFont(verdictFont);
	painter.setPen(ink);
	QString text;
	if (pairVerdict)
		text = state.verdictInputText + QStringLiteral("->") + state.verdictOutputText;
	else if (hasText)
		text = state.verdictText;
	else if (state.tone == VstBusFrameState::Tone::Success)
		text = QStringLiteral("ok");
	else
		// A wordless danger/warning verdict prints nothing here: the same
		// line already carries the "!"-tagged status message, and repeating
		// it one word earlier was the ink-register version of a second lamp.
		return;
	painter.drawText(QRectF(state.verdictRect), Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetricsF(verdictFont).elidedText(text, Qt::ElideRight, state.verdictRect.width()));
}

void MinimalSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// Bare ink, the approved reading: lowercase role, mono channel, painted
	// caret. No chrome; the states live entirely in the ink.
	const QRectF rect(state.rect);
	QFont roleFont(tokens.monoFontFamily);
	roleFont.setPixelSize(10);
	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(11);

	QColor roleInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	QColor valueInk(state.silent || state.defaulted ? tokens.mutedText : tokens.text);
	if (state.missingChannel)
		valueInk = QColor(tokens.danger);
	if (!state.enabled)
		valueInk = withAlpha(QColor(tokens.mutedText), 150);

	const QString role = state.roleToken.toLower();
	painter.setFont(roleFont);
	painter.setPen(roleInk);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(role);
	painter.drawText(QRectF(rect.left() + 3.0, rect.top(), roleWidth, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, role);

	painter.setFont(valueFont);
	painter.setPen(valueInk);
	const qreal valueLeft = rect.left() + 3.0 + roleWidth + 5.0;
	const qreal valueWidth = QFontMetricsF(valueFont).horizontalAdvance(state.valueText);
	painter.drawText(QRectF(valueLeft, rect.top(), rect.width() - (valueLeft - rect.left()), rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, state.valueText);

	const qreal caretLeft = valueLeft + valueWidth + 4.0;
	const qreal caretHalf = 2.5;
	const qreal caretMidY = rect.center().y() + 1.0;
	QColor caretInk = state.menuOpen || state.focused ? QColor(tokens.accent) : roleInk;
	painter.setRenderHint(QPainter::Antialiasing, true);
	QPainterPath caret;
	caret.moveTo(caretLeft, caretMidY - caretHalf / 2.0);
	caret.lineTo(caretLeft + caretHalf * 2.0, caretMidY - caretHalf / 2.0);
	caret.lineTo(caretLeft + caretHalf, caretMidY + caretHalf);
	caret.closeSubpath();
	painter.fillPath(caret, caretInk);
	painter.setRenderHint(QPainter::Antialiasing, false);

	if (state.enabled && (state.hovered || state.focused || state.menuOpen))
	{
		painter.setPen(QPen(state.focused || state.menuOpen ? QColor(tokens.accent) : roleInk, 1));
		painter.drawLine(QPointF(valueLeft, rect.bottom() - 2.5), QPointF(valueLeft + valueWidth, rect.bottom() - 2.5));
	}
}

void MinimalSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// No tray: the rail is negative space. Only the latch prints, in the
	// skin's reverse-video register while the strip is engaged.
	if (state.latchRect.isNull())
		return;
	QFont latchFont(tokens.monoFontFamily);
	latchFont.setPixelSize(10);
	painter.setFont(latchFont);
	const QString token = QStringLiteral("fill");
	const QRectF latch(state.latchRect);
	const qreal tokenWidth = QFontMetricsF(latchFont).horizontalAdvance(token);
	const QRectF chip(latch.left(), latch.center().y() - 8.0, tokenWidth + 10.0, 16.0);
	if (!state.collapsed)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(QColor(tokens.text), state.enabled ? 255 : 150));
		painter.fillRect(chip, painter.brush());
		painter.setPen(QColor(tokens.background));
	}
	else
	{
		if (state.latchHovered || state.latchFocused)
		{
			painter.setPen(QPen(state.latchFocused ? QColor(tokens.accent) : QColor(tokens.border), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(chip.adjusted(0.5, 0.5, -0.5, -0.5));
		}
		painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	}
	painter.drawText(chip, Qt::AlignCenter, token);
}
