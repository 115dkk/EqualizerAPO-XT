/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CrosspointMatrixRoutingRenderer.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QPainter>
#include <QFontMetrics>

using std::vector;

CrosspointMatrixView::CrosspointMatrixView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: GridRoutingViewBase(assignments, channelNames, portModel, parent, tokens,
		{ .rowHeaderWidth = 64, .colHeaderHeight = 30, .cellW = 52, .cellH = 26 },
		QStringLiteral("CrosspointEditor"), QStringLiteral("CrosspointChannelEditor"))
{
	rebuildMatrix();
}

void CrosspointMatrixView::updateMetrics()
{
	// The stock cell widths fit a Copy row's short device names (L, R,
	// SL, ...). The subwoofer crossover dialog posts channels like
	// FrontBass and SourceLFE on the same board, and a pill narrower than
	// its caption clipped it to fragments ("rontBas"). The caption font
	// decides the width a column really needs.
	QFont monoFont(skinTokens.monoFontFamily);
	monoFont.setPixelSize(12);
	const QFontMetrics fm(monoFont);

	int longestInput = 0;
	for (const QString& ch : matrix.inputs)
		longestInput = qMax(longestInput, fm.horizontalAdvance(ch));
	int longestOutput = 0;
	for (const QString& out : matrix.outputs)
		longestOutput = qMax(longestOutput, fm.horizontalAdvance(out));

	// Column labels inset the header cell by 6+6, row labels by 6+8, and the
	// caption keeps a little air inside the label.
	cellW = qMax(52, longestInput + 20);
	rowHeaderWidth = qMax(64, longestOutput + 22);
}

QRect CrosspointMatrixView::footerRect() const
{
	return QRect(0, colHeaderHeight + matrix.outputs.size() * cellH + 4, width(), footerH);
}

QSize CrosspointMatrixView::sizeHint() const
{
	int w = rowHeaderWidth + matrix.inputs.size() * cellW + 2;
	int h = colHeaderHeight + matrix.outputs.size() * cellH + 2;
	// The footer's caption cells need their own width when the grid above
	// is narrow (an emptied deviceless Copy has no columns at all).
	h += 4 + footerH;
	w = qMax(w, 220);
	return QSize(w, h);
}

void CrosspointMatrixView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor text(t.text);
	const QColor muted(t.mutedText);
	const QColor border(t.border);
	const QColor accent(t.accent);
	const QColor ok(t.success);
	const QColor danger(t.danger);

	QFont monoFont(t.monoFontFamily);
	monoFont.setPixelSize(12);
	QFont labelFont(monoFont);
	labelFont.setBold(true);
	const QFontMetrics labelMetrics(labelFont);

	removeRects.clear();
	removeRects.resize(matrix.outputs.size());

	// The axis labels are board coordinates printed in the label ink, with
	// no cell and no channel colour: the grid's own rules already frame
	// them, and colour on this board is rationed to state (the cells below
	// carry it). A virtual channel keeps the dash, as a rule under its name.
	const auto drawAxisLabel = [&](const QRect& area, const QString& name, bool virt, bool lit) {
		p.setFont(labelFont);
		p.setPen(lit ? text : muted);
		p.drawText(area, Qt::AlignCenter, name);
		if (virt)
		{
			const int w = labelMetrics.horizontalAdvance(name);
			const int left = area.center().x() - w / 2;
			const int y = area.center().y() + labelMetrics.ascent() / 2 + 2;
			p.setPen(QPen(muted, 1, Qt::DashLine));
			p.drawLine(left, y, left + w - 1, y);
		}
	};

	// Column headers (input channels; in fixed-source mode these are the IR
	// file's channel numbers, which are ports rather than virtual channels, so
	// they never take the virtual dash).
	for (int c = 0; c < matrix.inputs.size(); ++c)
	{
		const QString ch = matrix.inputs[c];
		const QRect hr(rowHeaderWidth + c * cellW, 0, cellW, colHeaderHeight);
		const bool virt = !portModel.fixedSourceMode() && portModel.isVirtualChannel(ch);
		drawAxisLabel(hr.adjusted(6, 6, -6, -8), ch, virt, false);
	}

	// Rows.
	for (int r = 0; r < matrix.outputs.size(); ++r)
	{
		const QString out = matrix.outputs[r];
		const QRect rr(0, colHeaderHeight + r * cellH, rowHeaderWidth, cellH);
		const bool virt = portModel.isVirtualChannel(out);
		const QRect labelRect = rr.adjusted(6, 4, -8, -4);
		drawAxisLabel(labelRect, out, virt, hoveredRow == r);
		p.setFont(monoFont);

		// A virtual bus can leave the board: hovering its row exposes an x
		// target at the end of its label (physical channels fold instead of
		// leaving, so they never get one).
		if (virt && hoveredRow == r)
		{
			const QRect xr(labelRect.right() - 14, labelRect.top(), 14, labelRect.height());
			p.setPen(text);
			p.drawText(xr, Qt::AlignCenter, QStringLiteral("x"));
			removeRects[r] = xr;
		}

		// Cells.
		for (int c = 0; c < matrix.inputs.size(); ++c)
		{
			const QRect cr = cellRect(r, c).adjusted(1, 1, -1, -1);
			const CopyRoutingAdapter::Cell cell = matrix.cell(r, c);

			p.setPen(QPen(border, 1));
			if (!cell.present)
			{
				p.setBrush(withAlpha(border, 18));
				p.drawRect(cr);
				continue;
			}

			QColor fill;
			QString label;
			const bool unity = cell.factor == 1.0 && !cell.isDecibel;
			if (!portModel.allowFactors)
			{
				// Factor-less routing (MultiConvolution): a crosspoint is either
				// patched or not, so the cell shows a connection dot, not a gain.
				fill = ok;
				label = QStringLiteral("●");
			}
			else if (cell.factor < 0)
			{
				fill = danger;
				label = cell.factor == -1.0 && !cell.isDecibel ? QStringLiteral("INV") : QString::number(cell.factor);
			}
			else if (unity)
			{
				fill = ok;
				label = QStringLiteral("1");
			}
			else
			{
				fill = accent;
				label = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QString::number(cell.factor);
			}

			p.setBrush(withAlpha(fill, 60));
			p.setPen(QPen(withAlpha(fill, 200), 1));
			p.drawRect(cr);
			p.setPen(text);
			p.drawText(cr, Qt::AlignCenter, label);
		}
	}

	// Footer strip: the board's expansion controls as mono caption KEYS. At
	// rest they wear the exact resting treatment of an empty crosspoint cell
	// (1px border rule + faint fill) - the pressable-cell form the grid above
	// has already taught - because a bare transparent caption does not read as
	// a button at all. Hover is the accent prelight.
	// +N CH reveals the folded device channels, +BUS patches a new virtual
	// bus onto the board.
	revealRect = QRect();
	addRect = QRect();
	p.setFont(monoFont);
	const QRect footer = footerRect();
	QFontMetrics fm(monoFont);
	int x = 4;

	auto captionCell = [&](const QString& caption, bool hovered) {
		const int w = fm.horizontalAdvance(caption) + 16;
		const QRect cr(x, footer.top(), w, footer.height());
		const QRect cell = cr.adjusted(0, 1, -1, -2);
		if (hovered)
		{
			p.setPen(QPen(withAlpha(accent, 200), 1));
			p.setBrush(withAlpha(accent, 24));
		}
		else
		{
			p.setPen(QPen(border, 1));
			p.setBrush(withAlpha(border, 18));
		}
		p.drawRect(cell);
		p.setPen(hovered ? text : muted);
		p.drawText(cr, Qt::AlignCenter, caption);
		x += w + 8;
		return cr;
	};

	if (fold.hiddenChannels > 0 || channelsExpanded)
	{
		const QString caption = channelsExpanded
			? QStringLiteral("FOLD")
			: QStringLiteral("+%1 CH").arg(fold.hiddenChannels);
		revealRect = captionCell(caption, hoveredControl == 1);
	}
	addRect = captionCell(QStringLiteral("+BUS"), hoveredControl == 2);
}

QRect CrosspointMatrixView::factorEditorRect(int outRow, int inCol) const
{
	return cellRect(outRow, inCol);
}

QRect CrosspointMatrixView::channelEditorRect() const
{
	return addRect.isNull()
		? QRect(4, footerRect().top(), 96, footerH)
		: addRect.adjusted(0, 0, 64, 0);
}

RoutingView* CrosspointMatrixRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new CrosspointMatrixView(assignments, channelNames, portModel, parent, tokens);
}
