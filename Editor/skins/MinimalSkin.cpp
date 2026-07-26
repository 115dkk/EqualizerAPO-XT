/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Minimal skin. Constitution: docs/skins/minimal.md. The file-scope
// instance is exposed through minimalSkin() so Skins::all() can assemble
// the roster without a central definition list.

#include "Skins.h"

#include <QFileDialog>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QStringList>
#include <QToolBar>
#include <QWidget>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/skins/pickers/MinimalFilterPicker.h"
#include "Editor/skins/cards/MinimalReferenceCardView.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "SkinFileIcons.h"
#include "SkinPaint.h"
#include "SkinSupport.h"

namespace
{
// ── Minimal ("The bank teller's terminal") helpers ──────────────────────────
// The arc trig lives in the shared SkinPaint.h (skinArcPoint).

// File-dialog pictograms in Minimal's language: square-cornered hairline
// outlines in body ink, drawn without antialiasing so every line lands on
// the pixel grid like the rest of the terminal. The console feel comes from
// the geometry (hard edges, hairlines, a filled cursor block as the only
// solid), NOT from ASCII art - the user wants "looks like a console", not a
// literal one (round-2 verdict). Icons must stay clearly visible.
class MinimalFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor ink(tokens.text);
		return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
			// Integer grid: everything derives from s and lands on whole
			// pixels; hairlines stay 1px up to 32, 2px above.
			painter.setRenderHint(QPainter::Antialiasing, false);
			const int s = sizePx;
			const int line = s >= 40 ? 2 : 1;
			painter.setPen(QPen(ink, line));
			painter.setBrush(Qt::NoBrush);
			const auto px = [s](double f) { return int(f * s + 0.5); };

			const auto docOutline = [&]() {
				// Sharp-cornered sheet with a squared step notch instead of a
				// diagonal fold: terminals do not do diagonals.
				const QPoint points[] = {
					{ px(0.25), px(0.12) }, { px(0.61), px(0.12) }, { px(0.61), px(0.26) },
					{ px(0.75), px(0.26) }, { px(0.75), px(0.88) }, { px(0.25), px(0.88) }
				};
				painter.drawPolygon(points, 6);
			};

			switch (glyph)
			{
			case Glyph::Folder:
				// Tab + body, and the terminal's one solid: a cursor block
				// parked in the tab.
				painter.drawRect(px(0.12), px(0.26), px(0.30), px(0.10));
				painter.drawRect(px(0.12), px(0.36), px(0.76), px(0.42));
				painter.fillRect(px(0.17), px(0.29), px(0.08), px(0.05), ink);
				break;
			case Glyph::ConfigFile:
				docOutline();
				painter.drawLine(px(0.33), px(0.44), px(0.67), px(0.44));
				painter.drawLine(px(0.33), px(0.56), px(0.67), px(0.56));
				painter.drawLine(px(0.33), px(0.68), px(0.55), px(0.68));
				break;
			case Glyph::AudioFile:
				docOutline();
				// A tiny level meter: three bars, the terminal's idea of audio.
				painter.fillRect(px(0.34), px(0.58), px(0.08), px(0.14), ink);
				painter.fillRect(px(0.46), px(0.48), px(0.08), px(0.24), ink);
				painter.fillRect(px(0.58), px(0.64), px(0.08), px(0.08), ink);
				break;
			case Glyph::PluginFile:
				docOutline();
				painter.drawRect(px(0.38), px(0.48), px(0.24), px(0.20));
				painter.drawLine(px(0.44), px(0.48), px(0.44), px(0.40));
				painter.drawLine(px(0.56), px(0.48), px(0.56), px(0.40));
				break;
			case Glyph::GenericFile:
				docOutline();
				break;
			case Glyph::Drive:
				painter.drawRect(px(0.12), px(0.32), px(0.76), px(0.36));
				painter.drawLine(px(0.20), px(0.56), px(0.50), px(0.56));
				painter.fillRect(px(0.72), px(0.52), px(0.08), px(0.08), ink);
				break;
			case Glyph::Computer:
				painter.drawRect(px(0.14), px(0.18), px(0.72), px(0.44));
				painter.fillRect(px(0.22), px(0.26), px(0.08), px(0.05), ink);
				painter.drawLine(px(0.50), px(0.62), px(0.50), px(0.74));
				painter.drawLine(px(0.34), px(0.78), px(0.66), px(0.78));
				break;
			}
		});
	}
};

// "The number is the control; the knob is confirmation." The figure is the
// brightest ink in the row - painted here when the widget supplies
// valueText, living in the adjacent ValueScrubBox (promoted by
// precision_*.qss) for the row dials, which supply none. The knob itself
// is a hairline instrument: a 1px 270-degree range arc, a travelled arc in
// text ink and a radial cursor tick at the value angle - no filled disc,
// no hub. Monochrome until dragged; dragging turns the travelled ink
// accent (active-state law).
void paintMinimalKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
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

// The GraphicEQ response plot as this skin's instrument: a measurement
// record on the console (dark) or the printed sheet (light). Every
// straight line is drawn crisp with antialiasing off; only the response
// curve keeps its antialiasing, because the curve is data.
void paintMinimalGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens)
{
	const QColor ground(tokens.graph);
	const QColor gridMinor(tokens.graphGridMinor);
	const QColor gridMajor(tokens.graphGridMajor);
	const QColor secondary(tokens.mutedText);
	const QColor bodyInk(state.enabled ? QColor(tokens.text) : QColor(tokens.mutedText));
	const QColor accent(tokens.accent);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(state.rect, ground);

	// Axis labels: secondary-ink mono print in the margins.
	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(7.5);
	painter.setFont(labelFont);

	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());

	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		const int x = qRound(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(x, plotTop, x, plotBottom);
		if (!line.label.isEmpty())
		{
			painter.setPen(secondary);
			painter.drawText(QRect(x - 24, plotBottom + 2, 48, state.rect.bottom() - plotBottom - 2),
				Qt::AlignHCenter | Qt::AlignTop, line.label);
		}
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		const int y = qRound(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(plotLeft, y, plotRight, y);
		if (!line.label.isEmpty())
		{
			painter.setPen(secondary);
			painter.drawText(QRect(state.rect.left(), y - 8, plotLeft - state.rect.left() - 4, 16),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
		}
	}

	// The 0 dB rule: the one full-strength straight line, body ink 1px.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		painter.setPen(QPen(bodyInk, 1));
		painter.drawLine(plotLeft, qRound(state.zeroY), plotRight, qRound(state.zeroY));
	}

	// Band-locked stems: 1px hairline verticals from the 0 dB rule to each
	// band level, in secondary ink so the response stays the brightest line.
	if (state.bandLocked)
	{
		painter.setPen(QPen(secondary, 1));
		const int base = qRound(qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom()));
		for (const QPointF& node : state.nodePositions)
			painter.drawLine(qRound(node.x()), base, qRound(node.x()), qRound(node.y()));
	}

	// The response: data, so it keeps its antialiasing. 1px, no fill.
	if (state.curve.size() >= 2)
	{
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(bodyInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(state.curve);
		painter.setRenderHint(QPainter::Antialiasing, false);
	}

	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const int x = qRound(state.nodePositions.at(i).x());
		const int y = qRound(state.nodePositions.at(i).y());
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.hoveredNode == i;
		if (selected)
		{
			// The inverted block. While a drag is live the dragged node is the
			// selected one under the pointer, and only it turns accent (the
			// active-state law); a disabled row's stored selection dims to the
			// secondary block - still inverted, no longer a live cursor.
			const QColor block = !state.enabled ? secondary : (hovered ? accent : bodyInk);
			painter.fillRect(QRect(x - 3, y - 3, 7, 7), block);
		}
		else
		{
			// A square hairline tick punched into the ground; hover fills it
			// exactly one background value step.
			painter.setPen(QPen(bodyInk, 1));
			painter.setBrush(hovered ? QColor(tokens.cardHover) : ground);
			painter.drawRect(x - 3, y - 3, 6, 6);
		}
		if (state.focused && state.focusedNode == i)
		{
			// The keyboard cursor: the square accent hairline frame.
			painter.setPen(QPen(QColor(tokens.focusRing), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(x - 5, y - 5, 10, 10);
		}
	}

	// Cursor readout: one secondary-ink mono line, top right.
	if (state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setPen(secondary);
		painter.setFont(labelFont);
		painter.drawText(QRectF(state.plotRect).adjusted(0, 2, -4, 0), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	// The frame: one square 1px hairline, exactly like the analysis graph;
	// keyboard focus swaps it for the accent hairline (focus grammar).
	painter.setPen(QPen(state.focused ? QColor(tokens.focusRing) : QColor(tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
}

// What the sheet is a record OF. A plotter sheet that does not say what it
// plotted is not a record, and the two new metrics have no danger tag and no
// dB in their figures to give it away. Magnitude keeps the RESPONSE masthead
// it has printed since this graph existed.
QString minimalSheetHeading(AnalysisMetric metric)
{
	switch (metric)
	{
	case AnalysisMetric::PhaseDegrees:
		return QStringLiteral("PHASE");
	case AnalysisMetric::GroupDelayMs:
		return QStringLiteral("GROUP DELAY");
	case AnalysisMetric::MagnitudeDb:
		break;
	}
	return QStringLiteral("RESPONSE");
}

// The analysis dock's response graph as this skin's plotter sheet: the
// measurement-record grammar of the GraphicEQ plot stretched into a wide
// always-on lab chart. Straight lines land on half-pixel centres so they
// stay crisp with antialiasing on. The sheet prints its own record header
// top-left, so an empty config's flat trace still reads as a deliberate
// record; the footer channel/sample-rate caption is sheet metadata, printed
// as-is (localized data) and elided, never overflowed.
void paintMinimalAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens)
{
	const QColor ground(tokens.graph);
	const QColor gridMinor(tokens.graphGridMinor);
	const QColor gridMajor(tokens.graphGridMajor);
	const QColor secondary(tokens.mutedText);
	const QColor bodyInk(tokens.text);
	// Every dB-only idea below is guarded on this. The magnitude sheet has to
	// print exactly as it did before phase and group delay existed, so a
	// branch that is not gated here is a regression, not an improvement.
	const bool magnitudeSheet = state.metric == AnalysisMetric::MagnitudeDb;

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	painter.fillRect(state.rect, ground);

	const double plotLeft = state.plotRect.left();
	const double plotRight = state.plotRect.right();
	const double plotTop = state.plotRect.top();
	const double plotBottom = state.plotRect.bottom();

	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(7.5);

	// Vertical grid and the frequency figures under the plot. A figure that
	// would run into the previous print is skipped (majors always print) -
	// the sheet stays legible at any dock width.
	painter.setFont(labelFont);
	const QFontMetricsF labelMetrics(labelFont);
	double lastFigureRight = -1.0e9;
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		const double x = qFloor(line.pos) + 0.5;
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(QPointF(x, plotTop), QPointF(x, plotBottom));
		if (line.label.isEmpty())
			continue;
		const double halfWidth = labelMetrics.horizontalAdvance(line.label) / 2.0;
		if (!line.major && x - halfWidth < lastFigureRight + 4.0)
			continue;
		painter.setPen(secondary);
		painter.drawText(QRectF(x - 24.0, plotBottom + 3.0, 48.0, 11.0),
			Qt::AlignHCenter | Qt::AlignTop, line.label);
		lastFigureRight = x + halfWidth;
	}

	// The value figures are printed exactly as the axis prepared them,
	// whichever metric is on the sheet. On a dB sheet they live in the side
	// margins, which are narrow, so the axis font follows the knob precedent:
	// shrink to fit, never clip. The other two metrics print theirs inboard
	// (below) where the margin is not the constraint, and keep the sheet's one
	// type size.
	QFont axisFont(labelFont);
	if (magnitudeSheet)
	{
		const double marginWidth = plotLeft - state.rect.left();
		double widest = 0.0;
		for (const AnalysisGraphState::GridLine& line : state.horizontal)
			widest = qMax(widest, QFontMetricsF(axisFont).horizontalAdvance(line.label));
		while (widest > marginWidth - 2.0 && axisFont.pointSizeF() > 6.0)
		{
			axisFont.setPointSizeF(axisFont.pointSizeF() - 0.5);
			widest = 0.0;
			for (const AnalysisGraphState::GridLine& line : state.horizontal)
				widest = qMax(widest, QFontMetricsF(axisFont).horizontalAdvance(line.label));
		}
	}
	painter.setFont(axisFont);
	double lastFigureY = 1.0e9;
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		const double y = qFloor(line.pos) + 0.5;
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(QPointF(plotLeft, y), QPointF(plotRight, y));
		if (line.label.isEmpty())
			continue;
		if (!line.major && lastFigureY - y < 11.0)
			continue;
		painter.setPen(line.major ? bodyInk : secondary);
		if (magnitudeSheet)
		{
			painter.drawText(QRectF(state.rect.left(), y - 6.0, plotLeft - state.rect.left() - 2.0, 12.0),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
			painter.drawText(QRectF(plotRight + 2.0, y - 6.0, state.rect.right() - plotRight - 2.0, 12.0),
				Qt::AlignLeft | Qt::AlignVCenter, line.label);
		}
		else
		{
			// A dB figure is three characters and the margins were cut to hold
			// exactly that. "-360" and "+2.00" do not fit them at any legible
			// size, and a figure that overflows its right-aligned box loses its
			// sign first: the sheet would print a wrong number, which is worse
			// than printing an ugly one. So on these sheets the scale moves
			// inboard and each figure sits ON its own rule (the picker's
			// caption-on-a-rule law), still at both edges because a 940px sheet
			// is read from whichever side is nearer. A rule against the pane's
			// top has no room above it and prints below instead. The trace is
			// drawn after this and crosses the figures the way a plotter's pen
			// crosses the scale already printed on the paper.
			const double advance = QFontMetricsF(axisFont).horizontalAdvance(line.label);
			const double baseline = y - plotTop < 12.0 ? y + 10.0 : y - 3.0;
			painter.drawText(QPointF(plotLeft + 4.0, baseline), line.label);
			painter.drawText(QPointF(plotRight - 4.0 - advance, baseline), line.label);
		}
		lastFigureY = y;
	}

	// The clipping flag: terminal error semantics. The region between the
	// trace and the 0 dB rule fills SOLID - reverse video, the way a
	// terminal marks a line that is wrong - and the trace prints through it
	// in the sheet's ground colour (the inverted glyph). The block's ink is
	// danger SUNK into the sheet's register (hue kept, saturation and value
	// derived down): a raw semantic red at area strength hurt the eyes in
	// both finishes - a terminal's error field is dim red, not neon.
	QPainterPath overshoot;
	const bool overshootValid = state.clipping && state.zeroY > plotTop + 1.0;
	const bool darkSheet = ground.lightness() < 128;
	if (overshootValid)
	{
		// One closed block per printed piece of the trace. Clipping is a
		// magnitude reading and magnitude prints in one piece, so in practice
		// this records once; a record with gaps still gets a block per piece
		// instead of one field spanning what was never measured.
		for (const QPolygonF& segment : state.curves)
		{
			if (segment.size() < 2)
				continue;
			QPolygonF closed = segment;
			closed.append(QPointF(segment.last().x(), state.zeroY));
			closed.append(QPointF(segment.first().x(), state.zeroY));
			overshoot.addPolygon(closed);
			overshoot.closeSubpath();
		}

		// Dark sheet: a dim red field (phosphor's error register). Light
		// sheet: a black-red block - on an ink-on-paper terminal the error
		// field is HEAVY ink, so the block goes near-ink dark with the red
		// hue kept, and the paper-coloured trace inverts through it white
		// against black.
		const QColor dangerBase(tokens.danger);
		const QColor errorBlock = QColor::fromHsvF(
			dangerBase.hsvHueF(),
			dangerBase.hsvSaturationF() * (darkSheet ? 0.70 : 0.84),
			dangerBase.valueF() * (darkSheet ? 0.56 : 0.55));

		QPainterStateGuard overshootState(&painter);
		painter.setClipRect(QRectF(plotLeft, plotTop, plotRight - plotLeft, state.zeroY - plotTop));
		painter.fillPath(overshoot, errorBlock);
	}

	// The zero rule: the one full-strength straight line, body ink 1px. It
	// prints only when the metric's zero lands inside the sheet. On the two new
	// metrics that is often the sheet's own edge - a group delay keeps zero in
	// its fit and measures upward from it, a descending phase starts at it - and
	// the rule is printed there all the same, because a baseline drawn along the
	// bottom or the top of a plotter sheet is still the axis the pen was zeroed
	// against.
	if (state.zeroVisible)
	{
		const double zeroY = qFloor(state.zeroY) + 0.5;
		painter.setPen(QPen(bodyInk, 1));
		painter.drawLine(QPointF(plotLeft, zeroY), QPointF(plotRight, zeroY));
	}

	// The response: a single 1px body-ink trace. No fill, no echo - the
	// trace is data and the brightest line on the sheet. Inside the error
	// block it inverts to ground ink (reverse video keeps the glyph). One
	// pass per piece: where the metric has no reading the pen lifts off the
	// sheet, and a stroke across the gap would print a measurement that was
	// never taken.
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;
		painter.setPen(QPen(bodyInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(segment);
		if (overshootValid)
		{
			QPainterStateGuard overshootSegmentState(&painter);
			painter.setClipRect(QRectF(plotLeft, plotTop, plotRight - plotLeft, state.zeroY - plotTop));
			painter.setClipPath(overshoot, Qt::IntersectClip);
			painter.setPen(QPen(ground, 1));
			painter.drawPolyline(segment);
		}
	}

	// The plotter crosshair: a full-height vertical hairline with a short
	// horizontal tick at the reading. Its ink rises from the secondary
	// half-tone to body ink with the hover progress (entry motion).
	const QColor crosshairInk = mixColor(secondary, bodyInk, qBound(0.0, state.hover, 1.0));
	if (state.cursorValid)
	{
		const double cursorX = qFloor(state.cursor.x()) + 0.5;
		const double readingY = qFloor(state.curveYAtCursor) + 0.5;
		painter.setPen(QPen(crosshairInk, 1));
		painter.drawLine(QPointF(cursorX, plotTop), QPointF(cursorX, plotBottom));
		// The short tick IS the reading, so it only prints where there is one.
		// Inside a null the metric has no value and curveYAtCursor falls back to
		// the axis floor; a tick parked there would print a measurement that was
		// never taken. The prepared readout is the state's own answer to whether
		// the column has a value, and a magnitude column always has one.
		if (magnitudeSheet || !state.cursorText.isEmpty())
			painter.drawLine(QPointF(cursorX - 6.0, readingY), QPointF(cursorX + 6.0, readingY));
	}

	// Top-margin annotations: the engraved sheet header (plus the clip tag
	// when the sheet is flagged) and the cursor reading, printed like a
	// plotter's margin note in the crosshair's rising ink.
	const QRectF topBand(plotLeft, state.rect.top() + 2.0, plotRight - plotLeft, 12.0);
	QFont captionFont(tokens.monoFontFamily);
	captionFont.setPointSizeF(7.5);
	captionFont.setBold(true);
	captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	painter.setFont(captionFont);
	painter.setPen(secondary);
	const QString heading = minimalSheetHeading(state.metric);
	painter.drawText(topBand, Qt::AlignLeft | Qt::AlignVCenter, heading);
	if (state.clipping)
	{
		// Error register, not annotation: the tag prints in danger ink like
		// the reverse-video block it labels.
		const double headingWidth = QFontMetricsF(captionFont).horizontalAdvance(heading);
		painter.setPen(QColor(tokens.danger));
		painter.drawText(topBand.adjusted(headingWidth + 12.0, 0.0, 0.0, 0.0),
			Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("!! OVER 0 DB"));
	}
	else if (!magnitudeSheet && !state.spanValueText.isEmpty())
	{
		// Figures first: the scale stays bare signed numbers in every metric,
		// so the unit is named once, here, on the line that identifies the
		// record. On a dB sheet the unit rode in the OVER tag and the cursor
		// readout; phase and group delay have no tag, and a column of signed
		// numbers that never names its unit is not a reading. The span arrives
		// already formatted - a degree sign is never spelled in this file - and
		// takes the slot the error tag holds on a magnitude sheet: one masthead,
		// one register. It prints in the data register (plain, as-is) rather
		// than the caption's tracked bold, because it carries values.
		const double headingWidth = QFontMetricsF(captionFont).horizontalAdvance(heading);
		painter.setFont(labelFont);
		painter.drawText(topBand.adjusted(headingWidth + 12.0, 0.0, 0.0, 0.0),
			Qt::AlignLeft | Qt::AlignVCenter, state.spanValueText);
	}
	if (state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setFont(labelFont);
		painter.setPen(crosshairInk);
		painter.drawText(topBand, Qt::AlignRight | Qt::AlignVCenter, state.cursorText);
	}
	else if (!magnitudeSheet && state.cursorValid)
	{
		// A terminal reports an empty result instead of dressing it up (the
		// picker's NO MATCH law). The crosshair still names the frequency; the
		// readout says the pen was off the sheet in that column.
		painter.setFont(labelFont);
		painter.setPen(secondary);
		painter.drawText(topBand, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("NO READING"));
	}

	// Sheet metadata on the bottom edge: the channel/sample-rate caption,
	// localized data printed as-is in secondary ink, elided to the sheet.
	if (!state.channelText.isEmpty())
	{
		painter.setFont(labelFont);
		painter.setPen(secondary);
		const QRectF footer(plotLeft, state.rect.bottom() - 14.0, plotRight - plotLeft, 12.0);
		painter.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(labelFont).elidedText(state.channelText, Qt::ElideRight, int(footer.width())));
	}

	// The frame: one square 1px hairline, the same frame the GraphicEQ
	// plot wears (half-pixel so it stays crisp under antialiasing).
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5));
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
void paintMinimalSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens)
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

// Leading type glyph for the line head, plain ASCII for the mapped types so
// every mono fallback font covers it. The glyph is shape information ("which
// kind of line is this"); the colour tag next to it stays the badge token.
QString minimalTypeGlyph(const QString& type)
{
	if (type == QStringLiteral("biquad"))
		return QStringLiteral("~");
	if (type == QStringLiteral("include"))
		return QStringLiteral(">>");
	if (type == QStringLiteral("vst"))
		return QStringLiteral("[]");
	if (type == QStringLiteral("copy"))
		return QStringLiteral("->");
	if (type == QStringLiteral("comment"))
		return QStringLiteral("#");
	if (type == QStringLiteral("spacer"))
		return QString();
	// Unmapped commands keep the fixed-width column so line heads stay
	// aligned; the middle dot (U+00B7) deliberately carries no further
	// meaning. Built from a code point so the source stays pure ASCII (no
	// /utf-8 flag is set for MSVC).
	return QString(QChar(0x00B7));
}

// ── Minimal (Ableton-like terminal, monospace) ──────────────────────────────
class MinimalSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("minimal"); }
	IRoutingRenderer* routingRenderer() const override
	{
		static StepListRoutingRenderer renderer;
		return &renderer;
	}
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		// The add-filter dropdown as a numbered terminal index; see
		// MinimalFilterPicker.h for the design.
		return new MinimalFilterPickerView(parent);
	}
	// The reference bodies as one line of type; see
	// MinimalReferenceCardView.h.
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new MinimalReferenceCardView(kind, parent);
	}
	// The neutral default keeps the shared stroke icons on the actions so the
	// File menu (which shares the QActions) stays modern; the toolbar buttons
	// themselves drop the pictures and show the command words instead
	// (precision_*.qss dresses them). Both calls set absolute state, so
	// re-running on every skin/dark switch is idempotent.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		if (toolBar == nullptr)
			return;
		ISkin::styleMainToolbar(toolBar, tokens);
		toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
	}

	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override
	{
		// Navigation keeps the shared stroke set in body ink; the entry
		// pictograms switch to the terminal's hairline provider.
		ISkin::styleFileDialog(dialog, tokens);
		if (dialog == nullptr)
			return;
		static MinimalFileIconProvider iconProvider;
		iconProvider.updateTokens(tokens);
		dialog->setIconProvider(&iconProvider);
	}
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		paintMinimalKnob(painter, rect, state, tokens);
	}
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override
	{
		paintMinimalGraphicEqPlot(painter, state, tokens);
	}
	void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const override
	{
		paintMinimalAnalysisGraph(painter, state, tokens);
	}
	// The metric switch (and, next in this campaign, an all-pass card's order):
	// one control, one grammar, both uses.
	void paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const override
	{
		paintMinimalSegmentedControl(painter, state, tokens);
	}
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// One flat line per command: 1px hairline box, square corners, and
		// state expressed as background-value steps only. Disabled rows fall
		// one step below the resting card; a line a false If branch swallowed
		// takes the same step down (to the engine both are dead code). The
		// '#' glyph and the readout column keep skipped and commented apart.
		const bool sunken = !info.enabled || info.lineSkipped;
		const QString background = sunken ? tokens.surface
			: (info.selected ? tokens.cardSelected : tokens.card);
		const QString borderColor = info.focused ? tokens.focusRing
			: (info.selected ? tokens.accent : tokens.border);
		QString style = QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 0; }")
			.arg(background, borderColor);
		if (!info.selected)
		{
			style += QStringLiteral(" QFrame#FilterCardRow:hover { background: %1; }")
				.arg(sunken ? tokens.card : tokens.cardHover);
		}
		return style;
	}
	QString cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const override
	{
		// No separate header plate: the row reads as a single text line, so
		// the header inherits the frame's background through transparency.
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0; }");
	}
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		Q_UNUSED(card);
		// A raw line (a bare note, or a programmatic command like If/EndIf
		// the editor does not model) is printed bare on the body strip: no
		// box, no input ground. The shared raw card lays its chrome inline,
		// so QSS cannot reach it and the override happens here. Rows are
		// rebuilt on skin/theme switches, so construction-time token values
		// stay current.
		if ((info.type == QStringLiteral("text") || info.type == QStringLiteral("if")
			|| info.type == QStringLiteral("eval") || info.dynamicLine) && body != nullptr)
		{
			const SkinTokens& tk = SkinManager::instance()->tokens();
			if (QLabel* rawText = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
			{
				rawText->setStyleSheet(QStringLiteral("QLabel#FilterCardRawText { background: transparent; color: %1; border: 0; border-radius: 0; padding: 2px 0; font-family: \"%2\"; }")
					.arg(info.enabled ? tk.text : tk.mutedText, tk.monoFontFamily));
			}
		}
		// Leading type glyph at the line head. Only modern card rows carry a
		// header here; the Include/VST body editors and the frozen legacy
		// rows consult the hook with header == nullptr and stay untouched.
		if (header == nullptr)
			return;
		QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
		if (headerLayout == nullptr)
			return;
		// A commented-out line leads with the comment marker it actually
		// carries in the config file; the marker is information, not decor.
		const QString glyph = info.enabled ? minimalTypeGlyph(info.type) : QStringLiteral("#");
		if (glyph.isEmpty())
			return;
		QLabel* glyphLabel = new QLabel(glyph, header);
		glyphLabel->setObjectName(QStringLiteral("MinimalTypeGlyph"));
		glyphLabel->setAlignment(Qt::AlignCenter);
		glyphLabel->setMinimumWidth(18);
		headerLayout->insertWidget(0, glyphLabel);
	}
	// The watch readout column: the analysis fact for a line is printed as
	// a right-aligned DM Mono column in the header. Painted here rather
	// than as a construction-time label because the facts refresh with
	// every analysis run and only paint time is guaranteed to see the
	// fresh values (prepareCommandRow would freeze the first, stale ones).
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool ifFamily = info.type == QStringLiteral("if");
		const bool verdictRow = ifFamily && info.command != QStringLiteral("endif");
		const bool valueRow = !ifFamily && (!info.evalText.isEmpty() || info.valueError);
		if (!verdictRow && !valueRow)
			return;

		QString figure;
		QColor ink;
		bool bold = false;
		if (verdictRow)
		{
			if (info.branchState == 1)
			{
				figure = QStringLiteral("TRUE");
				ink = QColor(tokens.text);
			}
			else if (info.branchState == 0)
			{
				figure = QStringLiteral("FALSE");
				ink = QColor(tokens.mutedText);
			}
			else if (info.branchState == 3)
			{
				figure = QStringLiteral("ERR");
				ink = QColor(tokens.text);
				bold = true;
			}
			else
			{
				// Unknown (-1) and short-circuited (2) read the same: the
				// register holds no measurement. The em dash (U+2014, built
				// from a code point - pure-ASCII source) sinks one step
				// below the secondary ink; no third grey token exists and
				// none is created here, the step is mixed from tokens.
				figure = QString(QChar(0x2014));
				ink = mixColor(QColor(tokens.mutedText), QColor(tokens.card), 0.45);
			}
		}
		else if (info.valueError)
		{
			figure = QStringLiteral("ERR");
			ink = QColor(tokens.text);
			bold = true;
		}
		else
		{
			figure = QStringLiteral("= ") + info.evalText;
			ink = QColor(tokens.mutedText);
		}

		// Right-aligned in the header band, ending ~205px short of the right
		// edge so the whole header button train (power / + / - / ..., which
		// spans just under 200px from the frame's right edge) keeps clear
		// ground; the readout is painted under the buttons, so anything
		// narrower prints beneath them and only a clipped sliver survives.
		// The If/Eval summaries are empty by model contract, so the column
		// prints on empty line space; a cramped card drops the readout
		// rather than colliding with the line head.
		const int headerHeight = qMin(tokens.rowHeight, rect.height());
		const QRect column(rect.left() + 8, rect.top(), rect.width() - 213, headerHeight);
		if (column.width() < 60)
			return;

		QFont font(tokens.monoFontFamily);
		font.setPointSizeF(9.0);
		font.setBold(bold);
		painter.setFont(font);
		painter.setPen(ink);
		painter.drawText(column, Qt::AlignRight | Qt::AlignVCenter,
			QFontMetrics(font).elidedText(figure, Qt::ElideRight, column.width()));
	}
	// The If-block scope in the gutter is a code editor's indent guide:
	// one crisp 1px border-ink hairline per scope level, antialiasing off.
	// The channel-group level is drawn by the same rule. Branch rows keep
	// their semantic indentation (logicSiblingsIndentAsMembers stays
	// false), so the innermost guide pauses on their line the way an
	// editor's guides do.
	bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// An unindented row has no gutter; leave the (empty) default path.
		if (info.depth <= 0)
			return false;
		painter.setRenderHint(QPainter::Antialiasing, false);
		// The guides go dashed where they pass a swallowed line: the dash
		// grammar ("no verified substance") extended to a stretch the engine
		// is not running (paired with the frame's background step in
		// cardFrameStyle).
		painter.setPen(QPen(QColor(tokens.border), 1, info.lineSkipped ? Qt::DotLine : Qt::SolidLine));
		const int unit = tokens.channelGroupIndent;
		for (int level = 0; level < info.depth; level++)
		{
			// Centred in its indent band, like RackChrome's level math; the
			// card face starts at x = 8 + depth * unit.
			const int x = 8 + level * unit + unit / 2;
			painter.drawLine(x, 0, x, size.height());
		}
		return true;
	}
	// The trailing add row is the terminal's input prompt line: "+ ADD
	// FILTER" as an uppercase tracked mono caption inside a 1px hairline
	// slot. No dashes: a dashed hairline means "no verified substance" in
	// this skin's chip grammar, and this slot is a real command.
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		QColor ink(tokens.mutedText);
		QColor edge(tokens.border);
		if (state.pressed)
		{
			painter.fillRect(rect, QColor(tokens.text));
			ink = QColor(tokens.surface);
			edge = QColor(tokens.text);
		}
		else if (state.hovered)
		{
			// One ground step plus the comment card's hover law: the caption
			// ink lifts to body brightness because the line acts on click.
			painter.fillRect(rect, QColor(tokens.surface));
			ink = QColor(tokens.text);
		}
		if (state.focused && !state.pressed)
			edge = QColor(tokens.focusRing);
		painter.setPen(QPen(edge, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect.adjusted(0, 0, -1, -1));

		QFont font(tokens.monoFontFamily);
		font.setPointSizeF(9.0);
		font.setWeight(QFont::Bold);
		font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(font);
		painter.setPen(ink);
		painter.drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
			QStringLiteral("+ ") + state.label.toUpper());
	}
	// The first-boundary seam: a text editor's insert line. The widget only
	// shows itself while hovered, so at rest nothing is painted anywhere.
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		if (!state.hovered && !state.pressed)
			return;
		const QColor accent(tokens.accent);
		const int centerY = rect.center().y();
		const int side = qMin(rect.height(), 12);
		const QRect cell(rect.left(), centerY - side / 2, side, side);

		painter.setPen(QPen(accent, 1));
		painter.setBrush(state.pressed ? QBrush(accent) : Qt::NoBrush);
		painter.drawRect(cell.adjusted(0, 0, -1, -1));
		painter.drawLine(cell.right() + 5, centerY, rect.right(), centerY);

		QFont font(tokens.monoFontFamily);
		font.setPixelSize(qMax(7, side - 3));
		font.setWeight(QFont::Bold);
		painter.setFont(font);
		painter.setPen(state.pressed ? QColor(tokens.background) : accent);
		painter.drawText(cell, Qt::AlignCenter, QStringLiteral("+"));
	}
	// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).
};
}

ISkin* minimalSkin()
{
	static MinimalSkin instance;
	return &instance;
}
