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

void MatrixSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
		const QColor ground(tokens.graph);
		const QColor borderInk(tokens.border);
		const QColor mutedInk(tokens.mutedText);
		const QColor textInk(tokens.text);
		const QColor accent(tokens.accent);
		const QColor cutInk(tokens.accent2);
		const QRect plot = state.plotRect.toRect();

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Disabled: content at low alpha; the dashed outer rule below is
		// drawn at full strength.
		if (!state.enabled)
			painter.setOpacity(0.45);

		painter.fillRect(state.rect, ground);

		QFont labelFont(tokens.monoFontFamily);
		labelFont.setPointSizeF(7.5);
		painter.setFont(labelFont);
		const QFontMetrics labelMetrics(labelFont);

		// Crisp 1px grid. The tokens' minor ink is the graph mesh; the major
		// rank is derived from muted ink at low alpha because the shared
		// major token equals the border ink, which the light board cannot
		// tell from the minor mesh. Labels speak DM Mono in muted ink,
		// minors one step quieter.
		QColor majorInk(mutedInk);
		majorInk.setAlpha(90);
		const QColor minorInk(tokens.graphGridMinor);
		QColor minorLabelInk(mutedInk);
		minorLabelInk.setAlpha(150);
		for (const GraphicEQPlotState::GridLine& line : state.vertical)
		{
			const int x = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(x, plot.top(), x, plot.bottom());
			if (!line.label.isEmpty())
			{
				painter.setPen(line.major ? mutedInk : minorLabelInk);
				painter.drawText(QRect(x - 24, plot.bottom() + 2, 48, state.rect.bottom() - plot.bottom() - 2),
					Qt::AlignHCenter | Qt::AlignTop, line.label);
			}
		}
		for (const GraphicEQPlotState::GridLine& line : state.horizontal)
		{
			const int y = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(plot.left(), y, plot.right(), y);
			if (!line.label.isEmpty())
			{
				painter.setPen(line.major ? mutedInk : minorLabelInk);
				painter.drawText(QRect(state.rect.left(), y - 8, plot.left() - state.rect.left() - 4, 16),
					Qt::AlignRight | Qt::AlignVCenter, line.label);
			}
		}

		// The 0 dB bus: a body-ink 1px rule, one rank of authority above the
		// grid.
		const bool zeroVisible = state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom();
		if (zeroVisible)
		{
			QColor zeroInk(textInk);
			zeroInk.setAlpha(180);
			painter.setPen(QPen(zeroInk, 1));
			painter.drawLine(plot.left(), int(state.zeroY), plot.right(), int(state.zeroY));
		}

		// Band-locked layouts: level stems off the 0 dB bus, in the LED
		// ring's bipolar grammar - boost lights accent, cut lights accent2.
		const double stemBase = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
		if (state.bandLocked)
		{
			for (const QPointF& node : state.nodePositions)
			{
				if (qAbs(node.y() - stemBase) < 1.0)
					continue;
				QColor stem(node.y() < stemBase ? accent : cutInk);
				stem.setAlpha(110);
				painter.setPen(QPen(stem, 2, Qt::SolidLine, Qt::FlatCap));
				painter.drawLine(QPointF(node.x(), stemBase), node);
			}
		}

		// The response trace: 2px accent, antialiased (the curve is data).
		// The fill stays ascetic - a bare wash in the variable layout only,
		// where no stems carry the level reading.
		if (state.curve.size() >= 2)
		{
			painter.setRenderHint(QPainter::Antialiasing, true);
			if (!state.bandLocked)
			{
				QPolygonF wash = state.curve;
				wash.append(QPointF(state.curve.last().x(), stemBase));
				wash.prepend(QPointF(state.curve.first().x(), stemBase));
				QColor washColor(accent);
				washColor.setAlpha(14);
				painter.setPen(Qt::NoPen);
				painter.setBrush(washColor);
				painter.drawPolygon(wash);
			}
			painter.setPen(QPen(accent, 2));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(state.curve);
			painter.setRenderHint(QPainter::Antialiasing, false);
		}

		// Crosspoint pre-light under the hovered node: a row and a column
		// hairline through the plot whose intersection is the node.
		if (state.enabled && state.hoveredNode >= 0 && state.hoveredNode < state.nodePositions.size())
		{
			const QPointF& hoverNode = state.nodePositions.at(state.hoveredNode);
			QColor hairline(accent);
			hairline.setAlpha(80);
			painter.setPen(QPen(hairline, 1));
			painter.drawLine(int(hoverNode.x()), plot.top(), int(hoverNode.x()), plot.bottom());
			painter.drawLine(plot.left(), int(hoverNode.y()), plot.right(), int(hoverNode.y()));
		}

		// Node cells: square crosspoints. Rest = an empty cell (opaque
		// ground punch + 1px muted rule, the resting-coordinate ink),
		// hover = accent rule + pre-light wash, selected = engaged (LED
		// fill + accent rule). The state ladder rest < hover < engaged.
		for (int i = 0; i < state.nodePositions.size(); i++)
		{
			const QPointF& center = state.nodePositions.at(i);
			const QRect cell(qRound(center.x()) - 3, qRound(center.y()) - 3, 7, 7);
			const bool selected = state.selectedNodes.contains(i);
			const bool nodeHovered = state.hoveredNode == i;
			if (selected)
			{
				painter.setPen(QPen(accent, 1));
				painter.setBrush(accent);
			}
			else if (nodeHovered)
			{
				QColor wash(accent);
				wash.setAlpha(48);
				painter.setPen(QPen(accent, 1));
				painter.setBrush(wash);
			}
			else
			{
				painter.setPen(QPen(mutedInk, 1));
				painter.setBrush(ground);
			}
			painter.drawRect(cell.adjusted(0, 0, -1, -1));
		}

		// The band the readout strip is addressing wears its coordinate tag
		// (mono, muted at rest, accent while engaged), and keyboard focus
		// brackets its cell square.
		if (state.focusedNode >= 0 && state.focusedNode < state.nodePositions.size())
		{
			const QPointF& focusNode = state.nodePositions.at(state.focusedNode);
			const QRect cell(qRound(focusNode.x()) - 3, qRound(focusNode.y()) - 3, 7, 7);
			const bool engaged = state.selectedNodes.contains(state.focusedNode);

			QFont tagFont(tokens.monoFontFamily);
			tagFont.setPointSizeF(7.0);
			tagFont.setBold(true);
			const QFontMetrics tagMetrics(tagFont);
			const QString tag = QString::number(state.focusedNode + 1);
			const int tagWidth = tagMetrics.horizontalAdvance(tag);
			int tagX = cell.right() + 5;
			if (tagX + tagWidth > plot.right() - 2)
				tagX = cell.left() - 5 - tagWidth;
			int tagY = cell.top() - 4;
			if (tagY - tagMetrics.ascent() < plot.top() + 2)
				tagY = cell.bottom() + 5 + tagMetrics.ascent();
			painter.setFont(tagFont);
			painter.setPen(engaged ? accent : mutedInk);
			painter.drawText(QPoint(tagX, tagY), tag);
			painter.setFont(labelFont);

			if (state.focused && state.enabled)
			{
				painter.setPen(QPen(accent, 1));
				painter.setBrush(Qt::NoBrush);
				painter.drawRect(cell.adjusted(-3, -3, 2, 2));
			}
		}

		// Cursor probe: a boxed sunken mono cell in the plot's top-right
		// corner.
		if (state.cursorValid && !state.cursorText.isEmpty())
		{
			QFont probeFont(tokens.monoFontFamily);
			probeFont.setPointSizeF(7.5);
			probeFont.setBold(true);
			const QFontMetrics probeMetrics(probeFont);
			const int cellWidth = probeMetrics.horizontalAdvance(state.cursorText) + 12;
			const QRect probeRect(plot.right() - 6 - cellWidth, plot.top() + 6, cellWidth, MatrixMetrics::knobCellHeight);
			painter.setPen(QPen(borderInk, 1));
			painter.setBrush(QColor(tokens.surfaceSunken));
			painter.drawRect(probeRect.adjusted(0, 0, -1, -1));
			painter.setFont(probeFont);
			painter.setPen(textInk);
			painter.drawText(probeRect, Qt::AlignCenter, state.cursorText);
			painter.setFont(labelFont);
		}

		// Outer rule: keyboard focus engages it in accent, a bypassed row
		// cancels it with a dash at full ink (the dash itself is never
		// dimmed).
		painter.setOpacity(1.0);
		painter.setPen(QPen(state.enabled ? QColor(state.focused ? accent : borderInk) : borderInk, 1,
			state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
	}
