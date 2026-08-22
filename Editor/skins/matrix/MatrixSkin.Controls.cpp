/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "MatrixSkinDetail.h"

namespace
{

// Point on the 270-degree value arc; fraction 0 is bottom-left (7:30), 0.5 is
// 12 o'clock, 1 is bottom-right (4:30). Same sweep as the shared default
// knob; the trig itself lives in SkinPaint.h.
QPointF matrixRadialPoint(const QPointF& center, double radius, double fraction)
{
	return skinArcPoint(center, radius, -(135.0 + 270.0 * fraction));
}

}

void MatrixSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
		const QColor borderColor(tokens.border);
		const QColor accentColor(tokens.accent);
		const QColor cutColor(tokens.accent2);
		const QColor mutedColor(tokens.mutedText);

		// Reserve the bottom strip for the boxed numeric cell when the widget
		// supplies an authoritative value text (e.g. the Preamp card). Promoted
		// legacy dials show their value in the adjacent spin box instead.
		QRect knobArea = rect;
		if (!state.valueText.isEmpty())
			knobArea.adjust(0, 0, 0, -(MatrixMetrics::knobCellHeight + 2));

		QRectF inner = QRectF(knobArea).adjusted(5, 5, -5, -5);
		const double side = qMin(inner.width(), inner.height());
		const QRectF ringRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
		const QPointF center = ringRect.center();
		const double outerRadius = side / 2.0;
		const double innerRadius = qMax(outerRadius - 6.0, 1.0);
		const double bodyRadius = qMax(innerRadius - 3.0, 1.0);

		painter.setRenderHint(QPainter::Antialiasing);

		// Segment ring. An even count gives bipolar knobs a natural centre gap
		// at 12 o'clock; unipolar knobs use an odd count so a segment sits at
		// every position including the centre.
		const int segmentCount = state.bipolar ? 14 : 15;
		const int half = segmentCount / 2;
		int litFrom = 0;
		int litCount = 0;
		bool boost = true;
		if (state.bipolar)
		{
			const double deviation = state.ratio - 0.5;
			boost = deviation >= 0.0;
			litCount = qMin(half, qRound(qAbs(deviation) * 2.0 * half));
			litFrom = boost ? half : half - litCount;
		}
		else
		{
			litCount = qBound(0, qRound(state.ratio * segmentCount), segmentCount);
		}

		QColor litColor = state.bipolar && !boost ? cutColor : accentColor;
		// Lit-segment luminance is calibrated per mode: on the dark board
		// the LEDs gain headroom toward white so a lit cell clearly outshines
		// the ghost ring; the light tokens were derived for maximum contrast
		// on white, where lightening would only desaturate them.
		if (tokens.dark)
			litColor = litColor.lighter(112);
		if (state.dragging)
			litColor = litColor.lighter(125);
		else if (state.hovered)
			litColor = litColor.lighter(112);
		// The unlit ring stays visible at low alpha: the range geometry -
		// and the bipolar centre gap - must read even with nothing lit, the
		// way an unlit LED is still a visible part on the board. Muted ink
		// instead of border ink, which vanished against the light card.
		QColor trackColor(mutedColor);
		trackColor.setAlpha(state.enabled ? 80 : 40);

		for (int i = 0; i < segmentCount; i++)
		{
			const double fraction = (i + 0.5) / segmentCount;
			const bool lit = state.enabled && i >= litFrom && i < litFrom + litCount;
			// A lit cell is wider than a ghost cell: LEDs bloom, rules do not.
			QPen segmentPen(lit ? litColor : trackColor, lit ? 3.5 : 2.5, Qt::SolidLine, Qt::FlatCap);
			painter.setPen(segmentPen);
			painter.drawLine(matrixRadialPoint(center, innerRadius, fraction),
				matrixRadialPoint(center, outerRadius, fraction));
		}

		// Centre detent tick: marks the 0-position gap of bipolar knobs so the
		// two knob kinds read differently even at rest. Full text ink: at
		// 0 dB the gap plus this tick is the whole detent statement.
		if (state.bipolar)
		{
			painter.setPen(QPen(state.enabled ? QColor(tokens.text) : QColor(trackColor), 1.0, Qt::SolidLine, Qt::FlatCap));
			painter.drawLine(matrixRadialPoint(center, outerRadius + 1.0, 0.5),
				matrixRadialPoint(center, outerRadius + 4.0, 0.5));
		}

		QColor bodyColor(state.enabled ? tokens.card : tokens.surface);
		painter.setPen(QPen(borderColor, 1.0, state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(bodyColor);
		painter.drawEllipse(center, bodyRadius, bodyRadius);
		painter.setPen(QPen(state.enabled ? litColor : QColor(mutedColor), 2.0, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(matrixRadialPoint(center, bodyRadius * 0.45, state.ratio),
			matrixRadialPoint(center, bodyRadius - 1.5, state.ratio));

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Keyboard focus: a square cell bracket, not a glow.
		if (state.focused && state.enabled)
		{
			painter.setPen(QPen(accentColor, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(ringRect.toRect().adjusted(-3, -3, 3, 3));
		}

		// Boxed mono numeric cell: the authoritative reading.
		if (!state.valueText.isEmpty())
		{
			QFont monoFont(tokens.monoFontFamily);
			monoFont.setPointSizeF(7.5);
			monoFont.setBold(true);
			const QFontMetrics metrics(monoFont);
			const int cellWidth = qMin(rect.width(), metrics.horizontalAdvance(state.valueText) + 12);
			const QRect cellRect(rect.center().x() - cellWidth / 2,
				rect.bottom() - MatrixMetrics::knobCellHeight + 1, cellWidth, MatrixMetrics::knobCellHeight - 1);
			painter.setPen(QPen(state.dragging ? accentColor : borderColor, 1));
			painter.setBrush(QColor(tokens.surfaceSunken));
			painter.drawRect(cellRect);
			painter.setFont(monoFont);
			if (!state.enabled)
				painter.setPen(QColor(mutedColor));
			else if (state.dragging || state.hovered)
				painter.setPen(accentColor);
			else
				painter.setPen(QColor(tokens.text));
			painter.drawText(cellRect, Qt::AlignCenter, state.valueText);
		}
	}

void MatrixSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
		const QColor borderInk(tokens.border);
		const QColor mutedInk(tokens.mutedText);
		const QColor textInk(tokens.text);
		const QColor accent(tokens.accent);
		const QRect frame = state.rect;

		QPainterStateGuard painterState(&painter);
		// Invariant rule 7: no antialiasing under a 1px rule. Nothing here is
		// a curve, so only the type is smoothed.
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		// Sunken board ground, the recess every readout cell sits in.
		painter.fillRect(frame, QColor(tokens.surfaceSunken));
		if (state.labels.isEmpty() || frame.width() < 8 || frame.height() < 8)
		{
			painter.setPen(QPen(borderInk, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(frame.adjusted(0, 0, -1, -1));
			return;
		}

		// Cancelled: every slot stays on the board and loses its light. The
		// contents sink to low alpha; the outer rule takes the cancellation
		// dash at full ink further down.
		if (!state.enabled)
			painter.setOpacity(0.55);

		// The bus lane takes the bottom of the strip and the ports sit above
		// it. A strip too short for both keeps the ports and drops the lane
		// rather than crushing the two together.
		const bool hasLane = frame.height() >= 18;
		const int laneY = hasLane ? frame.bottom() - 3 : frame.bottom() + 2;
		const int cellTop = frame.top() + 2;
		const int cellBottom = laneY - 2;
		const int cellHeight = qMax(1, cellBottom - cellTop);
		const int lastIndex = static_cast<int>(state.labels.size()) - 1;
		// Both edges are rounded from the fractional cell, never the width
		// from one edge: that is what makes the ports tile the strip exactly
		// instead of leaving a seam that drifts along the bus.
		const auto columnSpan = [&](double index, int inset) {
			const QRectF seg = state.segmentRect(index);
			const int left = qRound(seg.left()) + inset;
			const int right = qRound(seg.right()) - inset;
			return QRect(left, cellTop, qMax(1, right - left), cellHeight);
		};
		const auto columnRect = [&](double index) { return columnSpan(index, 0); };

		// A cancelled strip answers to nothing: the pointer states are dropped
		// here once instead of being tested at every use below.
		const int hovered = state.enabled && state.hoveredIndex >= 0 && state.hoveredIndex <= lastIndex
			? state.hoveredIndex : -1;
		const int pressed = state.enabled && state.pressedIndex >= 0 && state.pressedIndex <= lastIndex
			? state.pressedIndex : -1;

		// Crosspoint pre-light: addressing a port lights the whole bus faintly
		// (the row band) and the addressed column firmly, so the cell reads as
		// an intersection rather than as a button. The column band carries far
		// more alpha than the picker's original 16-18, which measured about
		// 3.5% brightness on a real panel and was reported as "hover does not
		// highlight" (M2 recalibration).
		if (hovered >= 0)
		{
			painter.fillRect(frame.adjusted(1, 1, -1, -1), withAlpha(accent, 14));
			painter.fillRect(columnRect(hovered).adjusted(1, -1, -1, 1), withAlpha(accent, 40));
		}

		// The port dividers and the bus rule: crisp integer 1px board ruling.
		painter.setPen(QPen(borderInk, 1));
		for (int i = 1; i <= lastIndex; i++)
		{
			const int x = qRound(state.segmentRect(i).left());
			painter.drawLine(x, frame.top() + 1, x, hasLane ? laneY + 2 : frame.bottom() - 1);
		}
		if (hasLane)
			painter.drawLine(frame.left() + 1, laneY, frame.right() - 1, laneY);

		// The bus ladder: a resting port is border ink, the addressed port
		// takes accent at 1px, the engaged port takes accent at 2px. Three
		// ranks that cannot be mistaken for one another at arm's length.
		if (hasLane && hovered >= 0)
		{
			const QRect column = columnRect(hovered);
			painter.setPen(QPen(withAlpha(accent, 120), 1));
			painter.drawLine(column.left() + 1, laneY, column.right() - 1, laneY);
		}

		// Press is the engage preview: the 1px accent rule plus a fill well
		// clear of the pre-light, one step short of the lit block it is about
		// to become.
		if (pressed >= 0 && pressed != state.selectedIndex)
		{
			const QRect column = columnRect(pressed).adjusted(1, -1, -1, 1);
			painter.fillRect(column, withAlpha(accent, 90));
			painter.setPen(QPen(accent, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(column.adjusted(0, 0, -1, -1));
		}

		// The engaged port. Whole-pixel geometry so the fill and the label
		// clip below agree exactly and the travel steps instead of blurring.
		const double position = qBound(0.0, state.selectionPosition, double(lastIndex));
		const QRect mark = columnSpan(position, 2);
		QColor litInk(accent);
		// Pressing the port that is already engaged still answers: the lamp
		// brightens by the skin's own step rather than doing nothing.
		if (pressed >= 0 && pressed == state.selectedIndex)
			litInk = litInk.lighter(115);
		if (state.enabled)
		{
			painter.fillRect(mark, litInk);
		}
		else
		{
			// Hollow lamp: the choice stays posted, unlit.
			painter.setPen(QPen(mutedInk, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(mark.adjusted(0, 0, -1, -1));
		}

		// The patch: the block's footprint on the bus and the 1px drop that
		// plugs one into the other.
		if (hasLane)
		{
			const QColor patchInk = state.enabled ? litInk : mutedInk;
			painter.setPen(QPen(patchInk, 1));
			painter.drawLine(mark.center().x(), mark.bottom() + 1, mark.center().x(), laneY - 1);
			if (state.enabled)
				painter.fillRect(QRect(mark.left(), laneY, mark.width(), 2), patchInk);
			else
				painter.drawLine(mark.left(), laneY, mark.right(), laneY);
		}

		// Board type: one size and one weight in every cell (rank comes from
		// position and light, never from size), all caps, elided rather than
		// squeezed.
		QFont cellFont(tokens.monoFontFamily);
		cellFont.setPointSizeF(7.5);
		cellFont.setWeight(QFont::DemiBold);
		cellFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
		painter.setFont(cellFont);
		const QFontMetrics cellMetrics(cellFont);
		const auto drawLabels = [&](bool lit) {
			for (int i = 0; i <= lastIndex; i++)
			{
				const QRect column = columnRect(i);
				// A cancelled strip keeps its figures readable and lets the
				// opacity say it is cancelled; only a live board lights ink.
				QColor ink = mutedInk;
				if (state.enabled)
				{
					if (lit)
						ink = QColor(tokens.background);
					else if (i == hovered || i == pressed)
						ink = textInk;
				}
				painter.setPen(ink);
				painter.drawText(column, Qt::AlignCenter,
					cellMetrics.elidedText(state.labels.at(i).toUpper(), Qt::ElideRight,
						qMax(0, column.width() - 8)));
			}
		};
		{
			QPainterStateGuard unlitLabelState(&painter);
			QRegion unlit(frame);
			if (state.enabled)
				unlit -= QRegion(mark);
			painter.setClipRegion(unlit);
			drawLabels(false);
		}
		if (state.enabled)
		{
			QPainterStateGuard litLabelState(&painter);
			painter.setClipRect(mark);
			drawLabels(true);
		}

		// Keyboard focus brackets the engaged port at the port's own edges -
		// square corners, never a glow (the corner language is the
		// rectangle), and it travels with the patch.
		if (state.focused && state.enabled)
		{
			QRect bracket = columnRect(position).adjusted(0, -1, 0, 1);
			// The end ports would otherwise put their corners on the outer
			// rule, which paints last and would swallow them.
			bracket.setLeft(qMax(bracket.left(), frame.left() + 1));
			bracket.setRight(qMin(bracket.right(), frame.right() - 1));
			bracket.setTop(qMax(bracket.top(), frame.top() + 1));
			bracket.setBottom(qMin(bracket.bottom(), frame.bottom() - 1));
			const int leg = qBound(3, bracket.width() / 6, 6);
			painter.setPen(QPen(accent, 1));
			painter.drawLine(bracket.left(), bracket.top(), bracket.left() + leg, bracket.top());
			painter.drawLine(bracket.left(), bracket.top(), bracket.left(), bracket.top() + leg);
			painter.drawLine(bracket.right() - leg, bracket.top(), bracket.right(), bracket.top());
			painter.drawLine(bracket.right(), bracket.top(), bracket.right(), bracket.top() + leg);
			painter.drawLine(bracket.left(), bracket.bottom() - leg, bracket.left(), bracket.bottom());
			painter.drawLine(bracket.left(), bracket.bottom(), bracket.left() + leg, bracket.bottom());
			painter.drawLine(bracket.right(), bracket.bottom() - leg, bracket.right(), bracket.bottom());
			painter.drawLine(bracket.right() - leg, bracket.bottom(), bracket.right(), bracket.bottom());
		}

		// The strip's outer rule; a cancelled control cancels it with a dash
		// at full ink (the dash itself is never dimmed).
		painter.setOpacity(1.0);
		painter.setPen(QPen(borderInk, 1, state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(frame.adjusted(0, 0, -1, -1));
	}

// The VST3 bus contract as two coordinate cells on the feed line: 1px
// rectangles, no curvature, monochrome at rest - the role designation in
// muted mono, the layout as the cell's bright value, the width as a dim
// ":n" suffix. Hover is the crossing prelight (accent border + faint
// fill); a disabled cell is a cancelled departure, dashed on the board.
void MatrixSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF cell = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);

	// At rest the cell wears the pressable-cell grammar the board already
	// taught (the footer caption keys, gate #176: a bare outline does not
	// read as a button): 1px border rule plus the faint fill. Hover and an
	// open menu are the accent prelight; a disabled cell is a cancelled
	// departure, dashed and unfilled.
	if (state.enabled)
		painter.fillRect(cell, state.hovered || state.menuOpen
			? withAlpha(QColor(tokens.accent), 24) : withAlpha(QColor(tokens.border), 18));
	QPen borderPen(QColor(tokens.border), 1);
	if (!state.enabled)
		borderPen.setStyle(Qt::DashLine);
	else if (state.focused || state.menuOpen || state.hovered)
		borderPen.setColor(withAlpha(QColor(tokens.accent), state.hovered && !state.focused && !state.menuOpen ? 200 : 255));
	painter.setPen(borderPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(cell);

	QFont roleFont(tokens.monoFontFamily);
	roleFont.setPixelSize(8);
	roleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
	painter.setFont(roleFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	QRectF textRect = cell.adjusted(6.0, 0, -6.0, 0);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleToken);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);

	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(11);
	painter.setFont(valueFont);
	painter.setPen(state.enabled ? QColor(tokens.text) : QColor(tokens.mutedText));
	QRectF valueRect(textRect);
	valueRect.setLeft(textRect.left() + roleWidth + 6.0);
	painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, state.layoutText);

	if (state.channelCount > 0)
	{
		const qreal valueWidth = QFontMetricsF(valueFont).horizontalAdvance(state.layoutText);
		QFont countFont(tokens.monoFontFamily);
		countFont.setPixelSize(9);
		painter.setFont(countFont);
		painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 220 : 130));
		QRectF countRect(valueRect);
		countRect.setLeft(valueRect.left() + valueWidth);
		painter.drawText(countRect, Qt::AlignLeft | Qt::AlignVCenter,
			QStringLiteral(":%1").arg(state.channelCount));
	}

	// The dropdown caret, stacked from 1px rules so it stays crisp without
	// antialiasing (no font glyph decides its shape). Right-aligned in the
	// cell's padding, muted like the role designation.
	const int caretRight = qRound(cell.right()) - 5;
	const int caretMidY = qRound(cell.center().y());
	painter.setPen(Qt::NoPen);
	const QColor caretInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 130);
	painter.fillRect(QRect(caretRight - 4, caretMidY - 1, 5, 1), caretInk);
	painter.fillRect(QRect(caretRight - 3, caretMidY, 3, 1), caretInk);
	painter.fillRect(QRect(caretRight - 2, caretMidY + 1, 1, 1), caretInk);
}

// The joint is the board's ASCII ">" and the verdict is a status cell: a
// boxed strip with the abstract cell LED (a lit square, no bezel, no dome)
// and an uppercase mono remark. Colour is rationed to state, so only the
// LED wears the tone; the words stay muted ink.
void MatrixSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	QFont monoFont(tokens.monoFontFamily);
	monoFont.setPixelSize(11);
	painter.setFont(monoFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	painter.drawText(QRectF(state.jointRect), Qt::AlignCenter, QStringLiteral(">"));

	const bool pairVerdict = !state.verdictInputText.isEmpty() || !state.verdictOutputText.isEmpty();
	const bool hasText = pairVerdict || !state.verdictText.isEmpty();
	if (state.verdictRect.isEmpty()
		|| (!hasText && state.tone == VstBusFrameState::Tone::Neutral))
		return;

	QColor led(tokens.mutedText);
	bool lit = true;
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success: led = QColor(tokens.success); break;
	case VstBusFrameState::Tone::Warning: led = QColor(tokens.warning); break;
	case VstBusFrameState::Tone::Critical: led = QColor(tokens.danger); break;
	case VstBusFrameState::Tone::Neutral: lit = false; break;
	}
	if (!state.enabled)
		lit = false;

	// The verdict beacon: a control-room segment stack - three crisp bars
	// with 1px air, lit solid in the rationed tone, dim when unlit - and no
	// cell around it (r3 judging: a box around a lamp is furniture, and a
	// lone 4px dot looked cheap on the board). The stack borrows the VU
	// segment shape, so it reads as an indicator bank, not a fleck.
	const int segWidth = 7;
	const int segHeight = 2;
	const int segGap = 1;
	const int segCount = 3;
	const int beaconHeight = segCount * segHeight + (segCount - 1) * segGap;
	const int beaconLeft = state.verdictRect.left() + 1;
	const int beaconTop = state.verdictRect.center().y() - beaconHeight / 2;
	for (int i = 0; i < segCount; i++)
	{
		const QRect segment(beaconLeft, beaconTop + i * (segHeight + segGap), segWidth, segHeight);
		painter.fillRect(segment, lit ? led : withAlpha(QColor(tokens.mutedText), 70));
	}

	// The remark speaks only for a negotiated pair. Word verdicts stay
	// beacon-only: the port strip's device engraving already posts the ABI
	// ("EXTERNAL DEVICE · VST2"), so a VST2 remark here restated the board
	// one cell over (maintainer judgement, r3).
	if (!pairVerdict)
		return;

	QFont remarkFont(tokens.monoFontFamily);
	remarkFont.setPixelSize(10);
	painter.setFont(remarkFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	QRectF textRect(state.verdictRect);
	textRect.setLeft(beaconLeft + segWidth + 6.0);
	const QString text = state.verdictInputText + QStringLiteral(">") + state.verdictOutputText;
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetricsF(remarkFont).elidedText(text, Qt::ElideRight, textRect.width()));
}

void MatrixSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// Postings, not keys: the fill cells carry only a bottom rule, so they
	// never compete with the fully boxed bus cells. A missing channel is a
	// cancelled posting - dashed rule, danger ink.
	const QRectF cell = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);

	QFont roleFont(tokens.monoFontFamily);
	roleFont.setPixelSize(8);
	roleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
	painter.setFont(roleFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	QRectF textRect = cell.adjusted(4.0, 0, -4.0, 0);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleToken);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);

	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(11);
	painter.setFont(valueFont);
	QColor valueInk(state.silent || state.defaulted ? tokens.mutedText : tokens.text);
	if (state.missingChannel)
		valueInk = QColor(tokens.danger);
	if (!state.enabled)
		valueInk = QColor(tokens.mutedText);
	painter.setPen(valueInk);
	QRectF valueRect(textRect);
	valueRect.setLeft(textRect.left() + roleWidth + 5.0);
	painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, state.valueText);

	QColor ruleColor(tokens.border);
	if (state.missingChannel)
		ruleColor = QColor(tokens.danger);
	else if (state.enabled && (state.focused || state.menuOpen))
		ruleColor = QColor(tokens.accent);
	else if (state.enabled && state.hovered)
		ruleColor = withAlpha(QColor(tokens.accent), 200);
	QPen rulePen(ruleColor, 1);
	if (state.missingChannel || !state.enabled)
		rulePen.setStyle(Qt::DashLine);
	painter.setPen(rulePen);
	painter.drawLine(QPointF(cell.left() + 2.0, cell.bottom()), QPointF(cell.right() - 2.0, cell.bottom()));

	const qreal caretHalf = 2.0;
	const QPointF caretMid(cell.right() - 5.0, cell.center().y() + 0.5);
	painter.setRenderHint(QPainter::Antialiasing, true);
	QPainterPath caret;
	caret.moveTo(caretMid + QPointF(-caretHalf, -caretHalf / 2.0));
	caret.lineTo(caretMid + QPointF(caretHalf, -caretHalf / 2.0));
	caret.lineTo(caretMid + QPointF(0.0, caretHalf));
	caret.closeSubpath();
	painter.fillPath(caret, withAlpha(QColor(tokens.mutedText), state.enabled ? 220 : 130));
}

void MatrixSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// One departure-board rule above the postings; nothing else.
	const QRectF band(state.rect);
	painter.setPen(QPen(withAlpha(QColor(tokens.border), 160), 1));
	painter.drawLine(QPointF(band.left() + 2.0, band.top() + 0.5),
		QPointF(band.right() - 2.0, band.top() + 0.5));

	if (state.latchRect.isNull())
		return;
	// The fold is a boarding-gate cell: boxed and lit while the strip is
	// posted, dashed - a cancellation - while it is folded away.
	const QRectF gate = QRectF(state.latchRect).adjusted(0.5, 1.5, -0.5, -1.5);
	if (!state.collapsed)
		painter.fillRect(gate, withAlpha(QColor(tokens.accent), state.latchHovered || state.latchPressed ? 34 : 24));
	else if (state.latchHovered || state.latchPressed)
		painter.fillRect(gate, withAlpha(QColor(tokens.border), 18));
	QPen gatePen(state.collapsed ? QColor(tokens.border) : QColor(tokens.accent), 1);
	if (state.collapsed)
		gatePen.setStyle(Qt::DashLine);
	if (state.latchFocused)
		gatePen.setColor(QColor(tokens.accent));
	painter.setPen(gatePen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(gate);
	QFont gateFont(tokens.monoFontFamily);
	gateFont.setPixelSize(8);
	gateFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	painter.setFont(gateFont);
	painter.setPen(state.collapsed ? QColor(tokens.mutedText) : QColor(tokens.text));
	painter.drawText(gate, Qt::AlignCenter, QStringLiteral("FILL"));
}
