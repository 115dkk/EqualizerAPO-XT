/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Editor/skins/studio/routing/LightTraceRoutingRenderer.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <algorithm>
#include <functional>

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/routing/CopyRoutingAdapter.h"

using std::vector;

namespace
{
int sc(int px) { return GUIHelper::scale(px); }

// is-dark / withAlphaF live in the shared SkinPaint.h.
}

StudioRoutingView::StudioRoutingView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: RoutingView(parent), skinTokens(tokens), portModel(portModel),
	// Targets the command referenced stay on the glass for the whole session,
	// even if their last trace is deleted.
	pinnedChannels(RoutingFold::referencedTargets(assignments))
{
	StudioRoutingModel::PortConfig config;
	config.fixedSources = portModel.fixedSources;
	config.allowFactors = portModel.allowFactors;
	model.load(assignments, channelNames, config);

	// The card glass must show through: no background of our own.
	setAutoFillBackground(false);
	setMouseTracking(true);
	setFocusPolicy(Qt::ClickFocus);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
	relayout();
}

std::vector<Assignment> StudioRoutingView::assignments() const
{
	return model.assignments();
}

void StudioRoutingView::galleryShowcase(const QString& state)
{
	if (state == QLatin1String("expanded"))
	{
		channelsExpanded = true;
		relayout();
	}
	else if (state == QLatin1String("addChannel"))
	{
		openChannelEditor();
		if (channelEditor != nullptr)
			channelEditor->setText(QStringLiteral("VS"));
	}
}

QString StudioRoutingView::chipLabel(bool inputRow, int index) const
{
	if (inputRow)
	{
		if (model.constInput(index))
			return QStringLiteral("const");
		return model.inputPorts().value(index);
	}
	return model.outputPorts().value(index);
}

void StudioRoutingView::relayout()
{
	const SkinTokens& t = skinTokens;
	const int chipH = sc(22);
	const int gap = sc(10);
	const int marginX = sc(12);
	const int marginY = sc(8);
	const int traceZone = sc(72);

	QFont sans(t.fontFamily);
	sans.setPixelSize(sc(11));
	sans.setBold(true);
	QFont mono(t.monoFontFamily);
	mono.setPixelSize(sc(11));
	const QFontMetrics sansFm(sans);
	const QFontMetrics monoFm(mono);

	// The channel fold: a collapsed view lights out (hides) every seeded port
	// the command does not involve. Hidden ports keep their model index and
	// get a null rect, so trace indices stay stable; every trace endpoint is
	// lit and therefore always visible.
	const QStringList& inputPorts = model.inputPorts();
	const QStringList& outputPorts = model.outputPorts();
	inputVisible = QVector<bool>(inputPorts.size(), true);
	outputVisible = QVector<bool>(outputPorts.size(), true);
	hiddenOutputs = 0;
	QVector<bool> inputLit(inputPorts.size(), false);
	QVector<bool> outputLit(outputPorts.size(), false);
	for (const StudioRoutingModel::Trace& trace : model.traces())
	{
		if (trace.input >= 0 && trace.input < inputLit.size())
			inputLit[trace.input] = true;
		if (trace.output >= 0 && trace.output < outputLit.size())
			outputLit[trace.output] = true;
	}
	QSet<QString> pinnedUpper;
	for (const QString& name : pinnedChannels)
		pinnedUpper.insert(name.toUpper());

	for (int j = 0; j < outputPorts.size(); j++)
		outputVisible[j] = channelsExpanded || outputLit[j]
			|| pinnedUpper.contains(outputPorts[j].toUpper())
			|| j >= model.seededOutputCount();
	for (int i = 0; i < inputPorts.size(); i++)
	{
		if (portModel.fixedSourceMode())
		{
			inputVisible[i] = true;
			continue;
		}
		const bool isConst = model.constInput(i);
		inputVisible[i] = channelsExpanded || inputLit[i]
			|| (!isConst && (pinnedUpper.contains(inputPorts[i].toUpper())
				|| i >= model.seededInputCount()));
	}
	// Representative fallback: while nothing is routed, the first two
	// device channels stand in on both rows. Keyed on traces, not pins,
	// so a freshly added virtual chip keeps its counterparts to connect
	// to.
	if (!channelsExpanded && model.traces().isEmpty())
	{
		for (int j = 0; j < outputPorts.size() && j < qMin(2, model.seededOutputCount()); j++)
			outputVisible[j] = true;
		for (int i = 0; i < inputPorts.size() && i < qMin(2, model.seededInputCount()); i++)
			inputVisible[i] = true;
	}
	for (bool v : outputVisible)
		if (!v)
			hiddenOutputs++;

	auto rowRects = [&](const QStringList& labels, bool inputRow, int y) {
		QVector<QRect> rects;
		const QVector<bool>& visible = inputRow ? inputVisible : outputVisible;
		int x = marginX;
		for (int i = 0; i < labels.size(); i++)
		{
			if (!visible.value(i, true))
			{
				rects.append(QRect());
				continue;
			}
			const QString label = chipLabel(inputRow, i);
			const bool monoChip = (inputRow && portModel.fixedSourceMode())
				|| (inputRow && model.constInput(i));
			const int textW = (monoChip ? monoFm : sansFm).horizontalAdvance(label);
			const int w = qMax(sc(26), textW + sc(20));
			rects.append(QRect(x, y, w, chipH));
			x += w + gap;
		}
		return rects;
	};

	inputRects = rowRects(model.inputPorts(), true, marginY);
	const int outputY = marginY + chipH + traceZone;
	outputRects = rowRects(model.outputPorts(), false, outputY);

	// The fold's reveal chip trails the input row: a ghost readout ("+N" of
	// lights currently off, "fold" once everything burns) in the same dashed
	// ghost-glass grammar as the add chip below.
	revealRect = QRect();
	if (isEnabled() && (hiddenOutputs > 0 || channelsExpanded))
	{
		int revealX = marginX;
		for (const QRect& rect : inputRects)
			if (!rect.isNull())
				revealX = qMax(revealX, rect.right() + gap);
		const QString caption = channelsExpanded
			? QStringLiteral("fold")
			: QStringLiteral("+%1").arg(hiddenOutputs);
		revealRect = QRect(revealX, marginY, qMax(sc(30), monoFm.horizontalAdvance(caption) + sc(16)), chipH);
	}

	// The virtual-output entry point trails the output row (both modes: the
	// mapping grammar also takes new virtual targets).
	int ghostX = marginX;
	for (const QRect& rect : outputRects)
		if (!rect.isNull())
			ghostX = qMax(ghostX, rect.right() + gap);
	ghostRect = isEnabled() ? QRect(ghostX, outputY, sc(30), chipH) : QRect();

	// Trace curves with vertical tangents; factor readouts fan out along
	// traces that converge on one output (the 0.28..0.72 spread).
	traceShapes.clear();
	QHash<int, int> perOutput;
	for (const StudioRoutingModel::Trace& trace : model.traces())
		if (trace.input >= 0)
			perOutput[trace.output]++;
	QHash<int, int> seen;
	for (const StudioRoutingModel::Trace& trace : model.traces())
	{
		TraceShape shape;
		if (trace.input >= 0 && trace.input < inputRects.size()
			&& trace.output >= 0 && trace.output < outputRects.size())
		{
			const QPointF from = portPoint(true, trace.input);
			const QPointF to = portPoint(false, trace.output);
			const double dy = to.y() - from.y();
			shape.path.moveTo(from);
			shape.path.cubicTo(QPointF(from.x(), from.y() + 0.45 * dy),
				QPointF(to.x(), to.y() - 0.45 * dy), to);

			QPainterPathStroker stroker;
			stroker.setWidth(sc(10));
			shape.hit = stroker.createStroke(shape.path);

			const bool showFactor = model.allowFactors()
				&& (trace.factor != 1.0 || trace.isDecibel);
			if (showFactor)
			{
				shape.labelText = QString::number(trace.factor)
					+ (trace.isDecibel ? QStringLiteral(" dB") : QString());
				const int n = perOutput.value(trace.output);
				const int k = seen[trace.output]++;
				const double tPos = n <= 1 ? 0.5 : 0.28 + 0.44 * k / (n - 1);
				const QPointF center = shape.path.pointAtPercent(tPos);
				QFont labelFont(t.monoFontFamily);
				labelFont.setPixelSize(sc(10));
				const QSizeF size = QFontMetrics(labelFont).size(0, shape.labelText)
					+ QSizeF(sc(12), sc(6));
				shape.labelRect = QRectF(center.x() - size.width() / 2,
					center.y() - size.height() / 2, size.width(), size.height());
				shape.hit.addRect(shape.labelRect);
			}
		}
		traceShapes.append(shape);
	}

	// No syncSizeToHint here: relayout() runs from resizeEvent, where an
	// explicit resize would ping-pong with the host scroll area's own widget
	// resizing into unbounded recursion (observed as a 0xC00000FD gallery
	// crash). This view's hint HEIGHT is constant anyway - two chip rows and
	// the trace zone - so the height-pinning wrapper never needs a nudge.
	updateGeometry();
	update();
}

QRect StudioRoutingView::chipRect(bool inputRow, int index) const
{
	const QVector<QRect>& rects = inputRow ? inputRects : outputRects;
	return rects.value(index);
}

QPointF StudioRoutingView::portPoint(bool inputRow, int index) const
{
	// Derived from the chip rect (never an independent constant) so the trace
	// endpoints stay put under any DPI scale.
	const QRect rect = chipRect(inputRow, index);
	return inputRow
		? QPointF(rect.center().x(), rect.bottom() + sc(2))
		: QPointF(rect.center().x(), rect.top() - sc(2));
}

QSize StudioRoutingView::sizeHint() const
{
	const int chipH = sc(22);
	const int height = sc(8) * 2 + chipH * 2 + sc(72);
	int width = sc(24);
	for (const QRect& rect : inputRects)
		if (!rect.isNull())
			width = qMax(width, rect.right() + sc(12));
	for (const QRect& rect : outputRects)
		if (!rect.isNull())
			width = qMax(width, rect.right() + sc(12));
	if (!ghostRect.isNull())
		width = qMax(width, ghostRect.right() + sc(12));
	if (!revealRect.isNull())
		width = qMax(width, revealRect.right() + sc(12));
	return QSize(width, height);
}

QSize StudioRoutingView::minimumSizeHint() const
{
	// Same contract as the other painted routing views: report the content
	// size; the host scroll area (minimum pinned to 0) isolates it.
	return sizeHint();
}

void StudioRoutingView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	const bool dark = skinIsDark(t);
	const bool lit = isEnabled();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const QColor accent(t.accent);

	// Traces first (rest, then hovered, then selected on top), each glow a
	// stack of strokes of the one accent light.
	auto strokeTrace = [&](const QPainterPath& path, bool hovered, bool selected) {
		struct Layer { double width; int alpha; };
		QVector<Layer> layers;
		if (!lit)
			layers = { {1.2, 80} };
		else if (selected)
			layers = { {10.0, 36}, {7.0, 44}, {3.5, 125}, {1.6, 255} };
		else if (hovered)
			layers = { {7.0, 44}, {3.5, 125}, {1.6, 255} };
		else
			layers = { {6.0, 28}, {3.0, 85}, {1.4, 215} };
		for (int i = 0; i < layers.size(); i++)
		{
			int alpha = layers[i].alpha;
			// On light glass the outer halos bloom; trim them, keep the core.
			if (!dark && layers[i].width > 2.0)
				alpha = int(alpha * 0.75);
			QColor c = accent;
			c.setAlpha(alpha);
			p.setPen(QPen(c, sc(1) * layers[i].width, Qt::SolidLine, Qt::RoundCap));
			p.setBrush(Qt::NoBrush);
			p.drawPath(path);
		}
	};

	const QVector<StudioRoutingModel::Trace>& traces = model.traces();
	for (int pass = 0; pass < 3; pass++)
	{
		for (int i = 0; i < traceShapes.size(); i++)
		{
			if (traceShapes[i].path.isEmpty())
				continue;
			const bool selected = selectedTraces.contains(i);
			const bool hovered = hoveredTrace == i;
			const int order = selected ? 2 : hovered ? 1 : 0;
			if (order != pass)
				continue;
			strokeTrace(traceShapes[i].path, hovered, selected);
		}
	}

	// Port dots: quiet ring when idle, lit accent core + faked halo when a
	// trace lands there; the target end burns brighter than the source, which
	// is how the top-to-bottom flow states its direction (no arrowheads).
	QVector<bool> inputLit(inputRects.size(), false);
	QVector<bool> outputLit(outputRects.size(), false);
	for (const StudioRoutingModel::Trace& trace : traces)
	{
		if (trace.input >= 0 && trace.input < inputLit.size())
			inputLit[trace.input] = true;
		if (trace.output >= 0 && trace.output < outputLit.size())
			outputLit[trace.output] = true;
	}
	auto drawPort = [&](const QPointF& center, bool connected, bool isTarget) {
		p.setPen(Qt::NoPen);
		if (!connected || !lit)
		{
			p.setPen(QPen(QColor(t.border), sc(1)));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(center, sc(2), sc(2));
			return;
		}
		p.setBrush(withAlphaF(accent, dark ? 0.27 : 0.20));
		p.drawEllipse(center, sc(5), sc(5));
		p.setBrush(withAlphaF(accent, isTarget ? 1.0 : 0.78));
		p.drawEllipse(center, sc(2), sc(2));
	};
	for (int i = 0; i < inputRects.size(); i++)
		if (!inputRects[i].isNull())
			drawPort(portPoint(true, i), inputLit[i], false);
	for (int i = 0; i < outputRects.size(); i++)
		if (!outputRects[i].isNull())
			drawPort(portPoint(false, i), outputLit[i], true);

	// Chips: the lit-glass-chip grammar. Ink is the channel's data colour
	// (neutral instrument digits in fixed-source mode); the glass follows the
	// type-badge alpha formula, hover raises luminance one step, disabled
	// switches the pane off.
	QFont sans(t.fontFamily);
	sans.setPixelSize(sc(11));
	sans.setBold(true);
	QFont mono(t.monoFontFamily);
	mono.setPixelSize(sc(11));

	auto drawChip = [&](const QRect& rect, bool inputRow, int index) {
		const QString label = chipLabel(inputRow, index);
		const bool isConst = inputRow && model.constInput(index);
		const bool monoChip = (inputRow && portModel.fixedSourceMode()) || isConst;
		const bool hovered = lit && hoveredChip == index && hoveredChipIsInput == inputRow;
		const bool virt = !monoChip && CopyRoutingAdapter::isVirtualChannel(label);

		QColor ink = monoChip ? QColor(t.text) : QColor(CopyRoutingAdapter::channelColor(label));
		if (!dark && !monoChip)
			ink = ink.darker(120);
		if (isConst)
			ink = QColor(t.mutedText);

		double fillA = virt ? 0.08 : (dark ? 0.15 : 0.10);
		double borderA = dark ? 0.42 : 0.45;
		if (isConst)
			fillA = 0.06;
		if (hovered)
		{
			fillA += 0.07;
			borderA = 0.70;
		}
		if (!lit)
		{
			fillA = 0.06;
			borderA = 0.25;
		}

		QColor fill = monoChip ? QColor(255, 255, 255) : ink;
		if (monoChip && !dark)
			fill = QColor(t.card);
		p.setPen(QPen(withAlphaF(monoChip ? QColor(t.border) : ink, monoChip ? 1.0 : borderA),
			sc(1), (virt || isConst) ? Qt::DashLine : Qt::SolidLine));
		p.setBrush(monoChip ? withAlphaF(fill, dark ? 0.05 : 1.0) : withAlphaF(fill, fillA));
		p.drawRoundedRect(rect, sc(8), sc(8));

		// The glass formula's 1px top reflection (dark only; white glass
		// cannot get brighter, so light mode thickens the bottom edge).
		if (dark)
		{
			QColor reflect(255, 255, 255, lit ? (hovered ? 50 : 30) : 12);
			p.setPen(QPen(reflect, 1));
			p.drawLine(rect.left() + sc(8), rect.top() + 1, rect.right() - sc(8), rect.top() + 1);
		}
		else
		{
			p.setPen(QPen(withAlphaF(ink, 0.18), 1));
			p.drawLine(rect.left() + sc(8), rect.bottom(), rect.right() - sc(8), rect.bottom());
		}

		p.setFont(monoChip ? mono : sans);
		p.setPen(lit ? ink : withAlphaF(ink, 0.5));
		p.drawText(rect, Qt::AlignCenter, label);
	};
	removeRect = QRect();
	removeChip = -1;
	for (int i = 0; i < inputRects.size(); i++)
		if (!inputRects[i].isNull())
			drawChip(inputRects[i], true, i);
	for (int i = 0; i < outputRects.size(); i++)
	{
		if (outputRects[i].isNull())
			continue;
		drawChip(outputRects[i], false, i);

		// A virtual output can leave the glass: hovering its chip lights a
		// small x pane at the chip's shoulder (device channels fold instead
		// of leaving, so they never get one).
		const QString label = chipLabel(false, i);
		if (lit && hoveredChip == i && !hoveredChipIsInput
			&& CopyRoutingAdapter::isVirtualChannel(label))
		{
			const QRect chip = outputRects[i];
			const QRect xr(chip.right() - sc(7), chip.top() - sc(7), sc(14), sc(14));
			p.setPen(QPen(withAlphaF(QColor(t.mutedText), 0.65), sc(1)));
			p.setBrush(withAlphaF(QColor(t.graph), 0.92));
			p.drawEllipse(xr);
			p.setPen(QPen(withAlphaF(QColor(t.text), 0.85), sc(1) * 1.2, Qt::SolidLine, Qt::RoundCap));
			const QPointF c = QRectF(xr).center();
			p.drawLine(QPointF(c.x() - sc(3), c.y() - sc(3)), QPointF(c.x() + sc(3), c.y() + sc(3)));
			p.drawLine(QPointF(c.x() - sc(3), c.y() + sc(3)), QPointF(c.x() + sc(3), c.y() - sc(3)));
			removeRect = xr;
			removeChip = i;
		}
	}

	// Factor readouts: sunken glass windows on the trace (Copy mode only).
	if (model.allowFactors())
	{
		QFont labelFont(t.monoFontFamily);
		labelFont.setPixelSize(sc(10));
		p.setFont(labelFont);
		for (int i = 0; i < traceShapes.size(); i++)
		{
			const TraceShape& shape = traceShapes[i];
			if (shape.labelRect.isNull())
				continue;
			const bool active = lit && (selectedTraces.contains(i) || hoveredTrace == i);
			p.setPen(QPen(active ? withAlphaF(accent, 0.60) : withAlphaF(QColor(t.border), 0.90), sc(1)));
			p.setBrush(withAlphaF(QColor(t.graph), 0.90));
			p.drawRoundedRect(shape.labelRect, sc(6), sc(6));
			p.setPen(!lit ? withAlphaF(QColor(t.mutedText), 0.47)
				: active ? QColor(t.text) : QColor(t.mutedText));
			p.drawText(shape.labelRect, Qt::AlignCenter, shape.labelText);
		}
	}

	// The virtual-output entry point: a ghost chip with a drawn + glyph
	// (no icon asset can wear this skin's ink).
	if (!ghostRect.isNull())
	{
		const double borderA = ghostHovered ? 0.80 : 0.40;
		const double fillA = ghostHovered ? 0.16 : 0.08;
		p.setPen(QPen(withAlphaF(accent, borderA), sc(1), Qt::DashLine));
		p.setBrush(withAlphaF(accent, fillA));
		p.drawRoundedRect(ghostRect, sc(8), sc(8));
		QColor glyph = accent;
		glyph.setAlpha(ghostHovered ? 255 : 200);
		p.setPen(QPen(glyph, sc(1) * 1.6, Qt::SolidLine, Qt::RoundCap));
		const QPointF c = QRectF(ghostRect).center();
		p.drawLine(QPointF(c.x() - sc(5), c.y()), QPointF(c.x() + sc(5), c.y()));
		p.drawLine(QPointF(c.x(), c.y() - sc(5)), QPointF(c.x(), c.y() + sc(5)));
	}

	// The fold's reveal chip (label set in relayout).
	if (!revealRect.isNull())
	{
		const double borderA = revealHovered ? 0.80 : 0.40;
		const double fillA = revealHovered ? 0.16 : 0.08;
		p.setPen(QPen(withAlphaF(accent, borderA), sc(1), Qt::DashLine));
		p.setBrush(withAlphaF(accent, fillA));
		p.drawRoundedRect(revealRect, sc(8), sc(8));
		QFont revealFont(t.monoFontFamily);
		revealFont.setPixelSize(sc(11));
		p.setFont(revealFont);
		QColor ink = accent;
		ink.setAlpha(revealHovered ? 255 : 200);
		p.setPen(ink);
		p.drawText(revealRect, Qt::AlignCenter, channelsExpanded
			? QStringLiteral("fold") : QStringLiteral("+%1").arg(hiddenOutputs));
	}

	// Drag-to-connect: a dashed provisional circuit; it solidifies over a
	// valid target (the release handler does the actual connect).
	if (dragging)
	{
		const QPointF from = portPoint(dragFromInput, dragChip);
		const QPointF to = dragPos;
		QPainterPath path;
		const double dy = to.y() - from.y();
		path.moveTo(from);
		path.cubicTo(QPointF(from.x(), from.y() + 0.45 * dy),
			QPointF(to.x(), to.y() - 0.45 * dy), to);
		bool overTarget = false;
		bool overInput = false;
		const int target = chipAt(dragPos, &overInput);
		if (target >= 0)
		{
			overTarget = overInput != dragFromInput
				|| (target != dragChip && chipHasTrace(dragFromInput, dragChip));
		}
		if (overTarget)
		{
			strokeTrace(path, true, false);
		}
		else
		{
			QColor c = accent;
			c.setAlpha(200);
			p.setPen(QPen(c, sc(1) * 1.4, Qt::DashLine, Qt::RoundCap));
			p.setBrush(Qt::NoBrush);
			p.drawPath(path);
		}
	}

	// Empty state: one quiet line in the trace zone, an instruction rather
	// than an alarm.
	if (traces.isEmpty() && lit && !dragging)
	{
		QFont hintFont(t.fontFamily);
		hintFont.setPixelSize(sc(11));
		p.setFont(hintFont);
		p.setPen(withAlphaF(QColor(t.mutedText), 0.78));
		const QRect zone(0, sc(8) + sc(22), width(), sc(72));
		p.drawText(zone, Qt::AlignCenter,
			tr("Drag between channels to route - click + to add an output"));
	}
}

void StudioRoutingView::resizeEvent(QResizeEvent*)
{
	relayout();
}

int StudioRoutingView::chipAt(const QPoint& pos, bool* inputRow) const
{
	for (int i = 0; i < inputRects.size(); i++)
		if (inputRects[i].adjusted(0, 0, 0, sc(4)).contains(pos))
		{
			*inputRow = true;
			return i;
		}
	for (int i = 0; i < outputRects.size(); i++)
		if (outputRects[i].adjusted(0, -sc(4), 0, 0).contains(pos))
		{
			*inputRow = false;
			return i;
		}
	return -1;
}

int StudioRoutingView::traceAt(const QPoint& pos) const
{
	for (int i = traceShapes.size() - 1; i >= 0; i--)
		if (!traceShapes[i].hit.isEmpty() && traceShapes[i].hit.contains(pos))
			return i;
	return -1;
}

bool StudioRoutingView::chipHasTrace(bool inputRow, int index) const
{
	for (const StudioRoutingModel::Trace& trace : model.traces())
		if ((inputRow && trace.input == index)
			|| (!inputRow && trace.output == index))
			return true;
	return false;
}

void StudioRoutingView::mousePressEvent(QMouseEvent* event)
{
	if (!isEnabled())
		return;

	if (!revealRect.isNull() && revealRect.contains(event->pos()))
	{
		channelsExpanded = !channelsExpanded;
		relayout();
		return;
	}

	if (!removeRect.isNull() && removeRect.contains(event->pos()) && removeChip >= 0)
	{
		const QString channel = model.outputPorts().value(removeChip);
		for (int i = pinnedChannels.size() - 1; i >= 0; i--)
			if (pinnedChannels[i].compare(channel, Qt::CaseInsensitive) == 0)
				pinnedChannels.removeAt(i);
		const bool changed = model.removeChannel(channel);
		selectedTraces.clear();
		hoveredTrace = -1;
		hoveredChip = -1;
		relayout();
		if (changed)
			emit routingChanged();
		return;
	}

	if (!ghostRect.isNull() && ghostRect.contains(event->pos()))
	{
		openChannelEditor();
		return;
	}

	bool inputRow = false;
	const int chip = chipAt(event->pos(), &inputRow);
	if (chip >= 0)
	{
		// A press arms drag-to-connect; if no drag develops, the release
		// selects the chip's traces.
		dragChip = chip;
		dragFromInput = inputRow;
		dragging = false;
		dragStart = event->pos();
		dragPos = event->pos();
		return;
	}

	const int trace = traceAt(event->pos());
	if (trace >= 0)
	{
		if (event->modifiers() & Qt::ControlModifier)
		{
			if (selectedTraces.contains(trace))
				selectedTraces.remove(trace);
			else
				selectedTraces.insert(trace);
		}
		else
		{
			selectedTraces = { trace };
		}
		update();
		return;
	}

	if (!selectedTraces.isEmpty())
	{
		selectedTraces.clear();
		update();
	}
}

void StudioRoutingView::mouseMoveEvent(QMouseEvent* event)
{
	if (dragChip >= 0)
	{
		dragPos = event->pos();
		if (!dragging && (dragPos - dragStart).manhattanLength() > sc(4))
			dragging = true;
		if (dragging)
			update();
		return;
	}

	bool inputRow = false;
	int chip = chipAt(event->pos(), &inputRow);
	// The x pane sits on the chip's shoulder, outside the chip rect; while
	// the pointer is over it the chip must stay hovered or the pane would
	// vanish before it can be clicked.
	if (chip < 0 && !removeRect.isNull() && removeRect.contains(event->pos()))
	{
		chip = removeChip;
		inputRow = false;
	}
	const int trace = chip >= 0 ? -1 : traceAt(event->pos());
	if (chip != hoveredChip || (chip >= 0 && inputRow != hoveredChipIsInput)
		|| trace != hoveredTrace
		|| (!ghostRect.isNull() && ghostRect.contains(event->pos())) != ghostHovered
		|| (!revealRect.isNull() && revealRect.contains(event->pos())) != revealHovered)
	{
		hoveredChip = chip;
		hoveredChipIsInput = inputRow;
		hoveredTrace = trace;
		ghostHovered = !ghostRect.isNull() && ghostRect.contains(event->pos());
		revealHovered = !revealRect.isNull() && revealRect.contains(event->pos());
		setCursor(chip >= 0 || trace >= 0 || ghostHovered || revealHovered
			? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
}

void StudioRoutingView::mouseReleaseEvent(QMouseEvent* event)
{
	if (dragChip < 0)
		return;

	const int fromChip = dragChip;
	const bool fromInput = dragFromInput;
	const bool wasDragging = dragging;
	dragChip = -1;
	dragging = false;

	if (wasDragging)
	{
		bool targetIsInput = false;
		const int target = chipAt(event->pos(), &targetIsInput);
		if (target >= 0 && targetIsInput != fromInput)
		{
			const int input = fromInput ? fromChip : target;
			const int output = fromInput ? target : fromChip;
			model.addTrace(input, output);
			relayout();
			emit routingChanged();
			return;
		}
		if (target >= 0 && targetIsInput == fromInput && target != fromChip
			&& model.rewirePort(fromInput, fromChip, target))
		{
			relayout();
			emit routingChanged();
			return;
		}
		update();
		return;
	}

	// Plain click: select every trace touching this chip.
	selectedTraces.clear();
	const QVector<StudioRoutingModel::Trace>& traces = model.traces();
	for (int i = 0; i < traces.size(); i++)
		if ((fromInput && traces[i].input == fromChip)
			|| (!fromInput && traces[i].output == fromChip))
			selectedTraces.insert(i);
	update();
}

void StudioRoutingView::mouseDoubleClickEvent(QMouseEvent* event)
{
	// Factor editing is a Copy-mode affordance; the mapping grammar has no
	// factors (RoutingPortModel contract).
	if (!isEnabled() || !model.allowFactors())
		return;
	const int trace = traceAt(event->pos());
	if (trace >= 0)
		openFactorEditor(trace);
}

void StudioRoutingView::keyPressEvent(QKeyEvent* event)
{
	if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
		&& !selectedTraces.isEmpty())
	{
		QList<int> ordered = selectedTraces.values();
		std::sort(ordered.begin(), ordered.end(), std::greater<int>());
		for (int index : ordered)
			model.removeTrace(index);
		selectedTraces.clear();
		hoveredTrace = -1;
		relayout();
		emit routingChanged();
		event->accept();
		return;
	}
	// No selection: let the row-level shortcuts have the key.
	event->ignore();
}

void StudioRoutingView::leaveEvent(QEvent*)
{
	hoveredChip = -1;
	hoveredTrace = -1;
	ghostHovered = false;
	revealHovered = false;
	update();
}

void StudioRoutingView::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::EnabledChange)
		relayout();
	RoutingView::changeEvent(event);
}

void StudioRoutingView::openFactorEditor(int trace)
{
	const QVector<StudioRoutingModel::Trace>& traces = model.traces();
	if (trace < 0 || trace >= traces.size())
		return;

	if (factorEditor == nullptr)
	{
		factorEditor = new QLineEdit(this);
		factorEditor->setObjectName(QStringLiteral("StudioRoutingFactorEditor"));
		factorEditor->setAlignment(Qt::AlignCenter);
		connect(factorEditor, &QLineEdit::editingFinished, this, &StudioRoutingView::commitFactorEditor);
	}

	factorEditorTrace = trace;
	QRectF rect = traceShapes.value(trace).labelRect;
	if (rect.isNull())
	{
		const QPointF center = traceShapes.value(trace).path.pointAtPercent(0.5);
		rect = QRectF(center.x() - sc(28), center.y() - sc(11), sc(56), sc(22));
	}
	const StudioRoutingModel::Trace& data = traces[trace];
	factorEditor->setGeometry(rect.toRect().adjusted(-sc(4), -sc(2), sc(4), sc(2)));
	factorEditor->setText(QString::number(data.factor) + (data.isDecibel ? QStringLiteral(" dB") : QString()));
	factorEditor->show();
	factorEditor->setFocus();
	factorEditor->selectAll();
}

void StudioRoutingView::commitFactorEditor()
{
	// editingFinished can fire twice (return + focus-out).
	if (factorEditor == nullptr || !factorEditor->isVisible() || factorEditorTrace < 0)
		return;

	const int trace = factorEditorTrace;
	factorEditorTrace = -1;
	const QString text = factorEditor->text();
	factorEditor->hide();

	model.setFactorText(trace, text);
	selectedTraces.clear();
	hoveredTrace = -1;
	relayout();
	emit routingChanged();
}

void StudioRoutingView::openChannelEditor()
{
	if (channelEditor == nullptr)
	{
		channelEditor = new QLineEdit(this);
		channelEditor->setObjectName(QStringLiteral("StudioRoutingChannelEditor"));
		channelEditor->setAlignment(Qt::AlignCenter);
		connect(channelEditor, &QLineEdit::editingFinished, this, &StudioRoutingView::commitChannelEditor);
	}
	channelEditor->setGeometry(ghostRect.adjusted(0, 0, sc(40), 0));
	channelEditor->setText(QString());
	channelEditor->show();
	channelEditor->setFocus();
}

void StudioRoutingView::commitChannelEditor()
{
	if (channelEditor == nullptr || !channelEditor->isVisible())
		return;

	const QString name = channelEditor->text().trimmed();
	channelEditor->hide();
	if (!RoutingFold::isValidChannelName(name))
		return;

	// No routingChanged: a fresh output has no sum yet, and the serializer
	// skips empty targets. Pinning keeps the new chip lit while it has no
	// trace yet.
	model.addOutput(name);
	CopyRoutingAdapter::pinChannel(pinnedChannels, name);
	relayout();
}

RoutingView* LightTraceRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new StudioRoutingView(assignments, channelNames, portModel, parent, tokens);
}
