/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BlockChipRoutingRenderer.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/flat/FlatPaint.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>


using std::vector;

BlockChipView::BlockChipView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: SumRoutingViewBase(assignments, channelNames, portModel, parent, tokens,
		SummandEditor::Factor, QStringLiteral("BlockFactorEditor"), QStringLiteral("BlockChannelEditor"))
{
	refold();
}

namespace
{
// A summand's factor as it is written in the row: nothing for a plain or an
// inverted channel (the sign before the box says "inverted"), otherwise the
// magnitude, or the decibel value with its own sign.
QString factorCaption(const Assignment::Summand& summand, bool allowFactors)
{
	if (!allowFactors)
		return QString();
	if (summand.isDecibel)
		return QStringLiteral("%1 dB").arg(summand.factor);
	if (summand.factor == 1.0 || summand.factor == -1.0)
		return QString();
	return QString::number(qAbs(summand.factor));
}

// The times sign between a factor and its channel, with the space around it.
const int timesWidth = 18;
// The inner padding of a summand box and the gap after it.
const int boxPadding = 9;
const int boxGap = 6;
// The summand boxes and the add box: the list's one cap height inside the
// filter list, their earlier 28 px in the subwoofer routing window.
const int windowBoxHeight = 28;

bool insideFilterList(const QWidget* widget)
{
	for (; widget != nullptr; widget = widget->parentWidget())
	{
		if (widget->objectName() == QLatin1String("FilterCardRow"))
			return true;
	}
	return false;
}
}

QSize BlockChipView::sizeHint() const
{
	const QFontMetrics fm(FlatPaint::numberFont(skinTokens, 14));
	int maxW = 240;
	for (int row : fold.visibleRows)
	{
		const Assignment& a = workingAssignments[row];
		// The target tag, the equals sign, the add button and the remove mark.
		int w = fm.horizontalAdvance(QString::fromStdWString(a.targetChannel)) + 18 + 26 + 34 + 30;
		for (const Assignment::Summand& s : a.sourceSum)
		{
			const QString factor = factorCaption(s, portModel.allowFactors);
			w += 18 + boxPadding * 2 + fm.horizontalAdvance(QString::fromStdWString(s.channel)) + boxGap;
			if (!factor.isEmpty())
				w += fm.horizontalAdvance(factor) + timesWidth;
		}
		maxW = qMax(maxW, w);
	}
	const int n = fold.visibleRows.size();
	int h = n * blockH + (n + 1) * gap;
	// The control row (reveal link + add-channel button) is always present.
	h += controlH + gap;
	return QSize(maxW + 24, h);
}

// Each output is one line of an equation, set the way a form sets it:
// the target as a tag, an equals sign, then every source as a box holding its
// factor and its channel. No card round the line and no colour per channel;
// the channel's name is what tells the channels apart.
void BlockChipView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor text(t.text), muted(t.mutedText);
	const QFont nameFont = FlatPaint::bodyFont(t, 13);
	const QFont valueFont = FlatPaint::numberFont(t, 14);
	const int boxHeight = insideFilterList(this) ? FlatPaint::listCapHeight : windowBoxHeight;
	const QFont channelFont = FlatPaint::bodyFont(t, 14, QFont::DemiBold);
	const QFont signFont = FlatPaint::bodyFont(t, 15);
	const QFontMetrics nameMetrics(nameFont);
	const QFontMetrics valueMetrics(valueFont);
	const QFontMetrics channelMetrics(channelFont);

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
		const int boxTop = y + (blockH - boxHeight) / 2;

		// The target: an outlined tag. A virtual channel's tag is dashed, the
		// skin's one mark for "this is not a device channel".
		const QString dest = QString::fromStdWString(a.targetChannel);
		const bool virtualTarget = portModel.isVirtualChannel(dest);
		int x = 4;
		const int tagWidth = nameMetrics.horizontalAdvance(dest) + 16;
		const QRect tag(x, y + (blockH - 22) / 2, tagWidth, 22);
		FlatPaint::box(p, tag, QColor(), QColor(virtualTarget ? t.controlEdge : t.borderStrong), 11.0, 1.0,
			virtualTarget ? Qt::DashLine : Qt::SolidLine);
		p.setFont(nameFont);
		p.setPen(text);
		p.drawText(tag, Qt::AlignCenter, dest);
		x += tagWidth + 8;

		p.setFont(signFont);
		p.setPen(muted);
		p.drawText(QRect(x, y, 16, blockH), Qt::AlignCenter, QStringLiteral("="));
		x += 24;

		for (int si = 0; si < (int)a.sourceSum.size(); ++si)
		{
			const Assignment::Summand& s = a.sourceSum[si];
			const QString ch = QString::fromStdWString(s.channel);
			// A decibel factor carries its own sign; only a linear factor is
			// split into a sign before the box and a magnitude inside it.
			const bool negative = !s.isDecibel && s.factor < 0;
			p.setFont(signFont);
			p.setPen(muted);
			if (si > 0 || negative)
			{
				p.drawText(QRect(x, y, 14, blockH), Qt::AlignCenter,
					negative ? QStringLiteral("−") : QStringLiteral("+"));
				x += 18;
			}

			// Fixed sources (IR file channels) are ports, not virtual
			// channels, so they keep the solid line.
			const bool virt = !portModel.fixedSourceMode() && portModel.isVirtualChannel(ch);
			const QString factor = factorCaption(s, portModel.allowFactors);
			const int factorWidth = factor.isEmpty() ? 0 : valueMetrics.horizontalAdvance(factor);
			const int channelWidth = channelMetrics.horizontalAdvance(ch);
			const int boxWidth = boxPadding * 2 + channelWidth + (factor.isEmpty() ? 0 : factorWidth + timesWidth);
			const QRect box(x, boxTop, boxWidth, boxHeight);
			FlatPaint::box(p, box, QColor(t.card), QColor(t.controlEdge), FlatPaint::controlRadius, 1.0,
				virt ? Qt::DashLine : Qt::SolidLine);

			int cx = box.left() + boxPadding;
			if (!factor.isEmpty())
			{
				p.setFont(valueFont);
				p.setPen(text);
				p.drawText(QRect(cx, box.top(), factorWidth, box.height()), Qt::AlignVCenter | Qt::AlignLeft, factor);
				// The factor is the editable part of the box.
				hits.append({ r, si, QRect(box.left(), box.top(), boxPadding + factorWidth, box.height()) });
				cx += factorWidth;
				p.setPen(muted);
				p.drawText(QRect(cx, box.top(), timesWidth, box.height()), Qt::AlignCenter, QStringLiteral("×"));
				cx += timesWidth;
			}
			else
			{
				// No factor shown: the whole box opens the editor.
				hits.append({ r, si, box });
			}
			p.setFont(channelFont);
			p.setPen(text);
			p.drawText(QRect(cx, box.top(), channelWidth + 4, box.height()), Qt::AlignVCenter | Qt::AlignLeft, ch);
			x += boxWidth + boxGap;
		}

		// Add a source to this line: a small button with a plus. This is what
		// makes an emptied line refillable.
		const QRect addBox(x + 2, boxTop, boxHeight, boxHeight);
		FlatPaint::box(p, addBox, QColor(t.card), QColor(t.borderStrong), FlatPaint::controlRadius);
		p.setPen(QPen(muted, 1.5, Qt::SolidLine, Qt::RoundCap));
		const QPointF plus = QRectF(addBox).center();
		p.drawLine(plus + QPointF(-4.5, 0.0), plus + QPointF(4.5, 0.0));
		p.drawLine(plus + QPointF(0.0, -4.5), plus + QPointF(0.0, 4.5));
		addHits.append({ r, addBox });
		x += boxHeight + 10;

		// A virtual channel's line can be removed: hovering the line shows a
		// cross after it (device channels fold instead of leaving).
		if (virtualTarget && hoveredRow == r)
		{
			const QRect cross(x, y + (blockH - 22) / 2, 22, 22);
			p.setPen(QPen(muted, 1.5, Qt::SolidLine, Qt::RoundCap));
			const QPointF mid = QRectF(cross).center();
			p.drawLine(mid + QPointF(-4.0, -4.0), mid + QPointF(4.0, 4.0));
			p.drawLine(mid + QPointF(4.0, -4.0), mid + QPointF(-4.0, 4.0));
			removeHits.append({ r, cross });
		}
	}

	// The control row: "show more channels" as a text link (underlined body
	// ink), then "add channel" as an ordinary small button.
	const int y = gap + fold.visibleRows.size() * (blockH + gap);
	const QFont controlFont = FlatPaint::bodyFont(t, 13);
	p.setFont(controlFont);
	const QFontMetrics fm(controlFont);
	const QColor linkInk(t.dark ? t.accent : t.text);
	int x = 4;
	if (fold.hiddenChannels > 0 || channelsExpanded)
	{
		const QString caption = channelsExpanded
			? tr("Show fewer channels")
			: tr("Show %n more channel(s)", nullptr, fold.hiddenChannels);
		const int w = fm.horizontalAdvance(caption) + 16;
		revealRect = QRect(x, y, w, controlH - 4);
		const bool hovered = hoveredControl == 1;
		if (hovered)
			FlatPaint::box(p, revealRect, QColor(t.cardHover), QColor(), FlatPaint::controlRadius, 0.0);
		p.setPen(linkInk);
		p.drawText(revealRect, Qt::AlignCenter, caption);
		const int underlineY = revealRect.center().y() + fm.ascent() / 2 + 3;
		p.fillRect(QRect(revealRect.left() + 8, underlineY, w - 16, 1), withAlphaF(linkInk, hovered ? 1.0 : 0.45));
		x += w + 10;
	}
	{
		const QString caption = tr("Add channel");
		const int w = fm.horizontalAdvance(caption) + 22 + 20;
		addChannelRect = QRect(x, y, w, controlH - 4);
		const bool hovered = hoveredControl == 2;
		FlatPaint::box(p, addChannelRect, QColor(hovered ? t.cardHover : t.card),
			QColor(hovered ? t.controlEdge : t.borderStrong), FlatPaint::controlRadius);
		p.setPen(QPen(text, 1.5, Qt::SolidLine, Qt::RoundCap));
		const QPointF plus(addChannelRect.left() + 15.0, QRectF(addChannelRect).center().y());
		p.drawLine(plus + QPointF(-4.0, 0.0), plus + QPointF(4.0, 0.0));
		p.drawLine(plus + QPointF(0.0, -4.0), plus + QPointF(0.0, 4.0));
		p.setPen(text);
		p.drawText(addChannelRect.adjusted(26, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, caption);
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
