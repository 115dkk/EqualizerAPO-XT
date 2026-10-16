/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BlockChipRoutingRenderer.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>

#include "Editor/widgets/routing/CopyRoutingAdapter.h"

using std::vector;

BlockChipView::BlockChipView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: SumRoutingViewBase(assignments, channelNames, portModel, parent, tokens,
		SummandEditor::Factor, QStringLiteral("BlockFactorEditor"), QStringLiteral("BlockChannelEditor"))
{
	refold();
}

static QFont uiFont(const SkinTokens& tokens, int px)
{
	QFont f(tokens.fontFamily);
	f.setPixelSize(px);
	return f;
}

QSize BlockChipView::sizeHint() const
{
	QFontMetrics fm(uiFont(skinTokens, 14));
	int maxW = 240;
	for (int row : fold.visibleRows)
	{
		const Assignment& a = workingAssignments[row];
		int w = 24 + fm.horizontalAdvance(QString::fromStdWString(a.targetChannel)) + 24 + 24 + 44 + 30; // + add chip + × target
		for (const Assignment::Summand& s : a.sourceSum)
			w += fm.horizontalAdvance(QString::fromStdWString(s.channel)) + 90;
		maxW = qMax(maxW, w);
	}
	const int n = fold.visibleRows.size();
	int h = n * blockH + (n + 1) * gap;
	h += controlH + gap;
	return QSize(maxW + 24, h);
}

void BlockChipView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const int radius = qMax(8, t.borderRadius);

	const QColor text(t.text), muted(t.mutedText), card(t.cardHover), border(t.border);
	const QColor ok(t.success), warn(t.warning), accent(t.accent);

	hits.clear();
	addHits.clear();
	removeHits.clear();
	revealRect = QRect();
	addChannelRect = QRect();

	for (int dr = 0; dr < fold.visibleRows.size(); ++dr)
	{
		const int r = fold.visibleRows[dr];
		const Assignment& a = workingAssignments[r];
		const int y = gap + dr * (blockH + gap);
		const QRect block(4, y, width() - 8, blockH);

		// Soft block background with a subtle accent edge.
		QPainterPath path;
		path.addRoundedRect(block, radius, radius);
		p.fillPath(path, card);
		p.setPen(QPen(withAlpha(accent, 70), 1.5));
		p.drawPath(path);

		const QString dest = QString::fromStdWString(a.targetChannel);
		const QColor destCol(CopyRoutingAdapter::channelColor(dest));
		p.setPen(Qt::NoPen);
		p.setBrush(destCol);
		p.drawRoundedRect(QRect(block.left() + 6, y, 5, blockH), 2, 2);

		QFont big = uiFont(skinTokens, 15);
		big.setBold(true);
		p.setFont(big);
		QFontMetrics bfm(big);
		int x = block.left() + 18;
		p.setPen(destCol);
		p.drawText(QRect(x, y, bfm.horizontalAdvance(dest) + 4, blockH), Qt::AlignVCenter | Qt::AlignLeft, dest);
		x += bfm.horizontalAdvance(dest) + 10;
		p.setPen(muted);
		p.drawText(QRect(x, y, 16, blockH), Qt::AlignCenter, QStringLiteral("="));
		x += 22;

		QFont chipFont = uiFont(skinTokens, 14);
		p.setFont(chipFont);
		QFontMetrics fm(chipFont);

		for (int si = 0; si < (int)a.sourceSum.size(); ++si)
		{
			const Assignment::Summand& s = a.sourceSum[si];
			const QString ch = QString::fromStdWString(s.channel);
			const bool neg = s.factor < 0;
			const bool showGain = s.factor != 1.0 || s.isDecibel;

			if (si > 0)
			{
				p.setPen(neg ? warn : muted);
				p.drawText(QRect(x, y, 16, blockH), Qt::AlignCenter, neg ? QStringLiteral("−") : QStringLiteral("+"));
				x += 18;
			}
			else if (neg)
			{
				p.setPen(warn);
				p.drawText(QRect(x, y, 12, blockH), Qt::AlignCenter, QStringLiteral("−"));
				x += 14;
			}

			QString factorText;
			if (showGain && portModel.allowFactors)
			{
				if (s.factor == -1.0 && !s.isDecibel)
					factorText = QStringLiteral("INV·");
				else
				{
					const double mag = neg ? -s.factor : s.factor;
					factorText = s.isDecibel ? QStringLiteral("%1dB·").arg(s.factor) : QStringLiteral("%1·").arg(mag);
				}
			}

			// Soft chip: factor·channel inside one rounded pill. Fixed sources
			// (IR file channels) are ports, not virtual channels, so they keep
			// the solid chip styling.
			const QColor col(CopyRoutingAdapter::channelColor(ch));
			const bool virt = !portModel.fixedSourceMode() && portModel.isVirtualChannel(ch);
			const int fw = fm.horizontalAdvance(factorText);
			const int cw = fm.horizontalAdvance(ch);
			const int chipW = fw + cw + 18;
			const QRect chip(x, y + (blockH - 26) / 2, chipW, 26);
			p.setPen(virt ? QPen(withAlpha(col, 180), 1, Qt::DashLine) : QPen(withAlpha(col, 120), 1));
			p.setBrush(withAlpha(col, virt ? 22 : 40));
			p.drawRoundedRect(chip, 13, 13);

			int cx = chip.left() + 9;
			if (!factorText.isEmpty())
			{
				p.setPen(text);
				p.drawText(QRect(cx, chip.top(), fw, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, factorText);
				// factor portion is the editable hit target
				hits.append({ r, si, QRect(cx, chip.top(), fw, chip.height()) });
				cx += fw;
			}
			else
			{
				// no factor shown: allow editing by clicking the chip
				hits.append({ r, si, chip });
			}
			p.setPen(col.darker(virt ? 100 : 130));
			QFont chBold = chipFont;
			chBold.setBold(true);
			p.setFont(chBold);
			p.drawText(QRect(cx, chip.top(), cw + 4, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, ch);
			p.setFont(chipFont);

			x += chipW + 8;
		}

		// Soft [+] chip per block: adds a source channel to this equation. This
		// is what makes an emptied Copy refillable from the GUI.
		const QRect addChip(x, y + (blockH - 26) / 2, 32, 26);
		p.setPen(QPen(withAlpha(accent, 140), 1, Qt::DashLine));
		p.setBrush(withAlpha(accent, 24));
		p.drawRoundedRect(addChip, 13, 13);
		p.setPen(withAlpha(accent, 220));
		p.drawText(addChip, Qt::AlignCenter, QStringLiteral("+"));
		addHits.append({ r, addChip });
		x += 40;

		// A virtual channel's block can be removed: hovering the block shows a
		// quiet × pill at its tail (device channels fold instead of leaving,
		// so they never get one). Muted, small, never alarming.
		if (portModel.isVirtualChannel(dest) && hoveredRow == r)
		{
			const QRect xChip(x, y + (blockH - 22) / 2, 22, 22);
			p.setPen(QPen(withAlpha(muted, 140), 1));
			p.setBrush(withAlpha(muted, 26));
			p.drawEllipse(xChip);
			p.setPen(withAlpha(muted, 230));
			p.drawText(xChip, Qt::AlignCenter, QStringLiteral("×"));
			removeHits.append({ r, xChip });
		}
	}

	// The control row: a quiet "show more channels" pill (OFF-state pill
	// grammar - sunken ground, 1px border, muted ink; hover raises it one
	// step) and the dashed "add channel" chip (the not-hardware-backed
	// grammar shared with the per-block [+]).
	const int y = gap + fold.visibleRows.size() * (blockH + gap);
	QFont chipFont = uiFont(skinTokens, 13);
	p.setFont(chipFont);
	QFontMetrics fm(chipFont);
	int x = 8;

	if (fold.hiddenChannels > 0 || channelsExpanded)
	{
		const QString caption = channelsExpanded
			? tr("Show fewer channels")
			: tr("Show %n more channel(s)", nullptr, fold.hiddenChannels);
		const int w = fm.horizontalAdvance(caption) + 24;
		revealRect = QRect(x, y, w, controlH - 4);
		const bool hovered = hoveredControl == 1;
		p.setPen(QPen(withAlpha(border, 160), 1));
		p.setBrush(hovered ? withAlpha(border, 60) : withAlpha(border, 30));
		p.drawRoundedRect(revealRect, (controlH - 4) / 2, (controlH - 4) / 2);
		p.setPen(hovered ? text : muted);
		p.drawText(revealRect, Qt::AlignCenter, caption);
		x += w + 10;
	}

	{
		const QString caption = QStringLiteral("+ ") + tr("Add channel");
		const int w = fm.horizontalAdvance(caption) + 24;
		addChannelRect = QRect(x, y, w, controlH - 4);
		const bool hovered = hoveredControl == 2;
		p.setPen(QPen(withAlpha(accent, hovered ? 220 : 140), 1, Qt::DashLine));
		p.setBrush(withAlpha(accent, hovered ? 40 : 24));
		p.drawRoundedRect(addChannelRect, (controlH - 4) / 2, (controlH - 4) / 2);
		p.setPen(withAlpha(accent, hovered ? 255 : 220));
		p.drawText(addChannelRect, Qt::AlignCenter, caption);
	}
}

int BlockChipView::slotAt(const QPoint& pos) const
{
	if (pos.y() < gap)
		return -1;
	return (pos.y() - gap) / (blockH + gap);
}

QRect BlockChipView::summandEditorRect(const QRect& span, int minimumWidth) const
{
	// The gain field covers the chip's factor portion (the whole chip for a
	// unity summand), widened to the right.
	Q_UNUSED(minimumWidth);
	return span.adjusted(-3, 0, 28, 0);
}

QRect BlockChipView::channelEditorRect() const
{
	const QRect anchor = addChannelRect.isNull()
		? QRect(8, gap + fold.visibleRows.size() * (blockH + gap), 120, controlH - 4)
		: addChannelRect;
	return anchor.adjusted(0, 0, 40, 0);
}

RoutingView* BlockChipRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new BlockChipView(assignments, channelNames, portModel, parent, tokens);
}
