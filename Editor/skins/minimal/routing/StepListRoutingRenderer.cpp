/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "StepListRoutingRenderer.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QPainter>
#include <QFontMetrics>

#include "Editor/skins/minimal/MinimalChannelInk.h"

using std::vector;

StepListView::StepListView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: SumRoutingViewBase(assignments, channelNames, portModel, parent, tokens,
		SummandEditor::SourceToken, QStringLiteral("StepSourceEditor"), QStringLiteral("StepChannelEditor"))
{
	refold();
}

static QFont monoFont(const SkinTokens& tokens)
{
	QFont f(tokens.monoFontFamily);
	f.setPixelSize(13);
	return f;
}

// Channel tokens print one step above the sheet's Medium (MinimalChannelInk.h).
static QFont channelFont(const SkinTokens& tokens)
{
	QFont f = monoFont(tokens);
	f.setWeight(QFont::DemiBold);
	return f;
}

QSize StepListView::sizeHint() const
{
	QFontMetrics fm(monoFont(skinTokens));
	QFontMetrics tokenMetrics(channelFont(skinTokens));
	int maxWidth = 220;
	for (int row : fold.visibleRows)
	{
		const Assignment& a = workingAssignments[row];
		int w = 36 + 52 + 28 + 26 + 26; // number + dest + arrow + [+] target + [x] target
		for (const Assignment::Summand& s : a.sourceSum)
		{
			const QString ch = QString::fromStdWString(s.channel);
			w += 18 + tokenMetrics.horizontalAdvance(ch) + 18 + fm.horizontalAdvance(QStringLiteral("x-0.000")) + 18;
		}
		maxWidth = qMax(maxWidth, w);
	}
	// The fold line (when channels are folded away) and the prompt/cursor
	// line that closes the listing.
	int extraLines = 1;
	if (fold.hiddenChannels > 0 || channelsExpanded)
		extraLines++;
	return QSize(maxWidth + 16, headerH + (fold.visibleRows.size() + extraLines) * rowH + 8);
}

// withAlpha lives in the shared SkinPaint.h.

void StepListView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	QPainter p(this);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	const QFont mono = monoFont(skinTokens);
	p.setFont(mono);
	QFontMetrics fm(mono);
	const QFont tokenFont = channelFont(skinTokens);
	const QFontMetrics tokenMetrics(tokenFont);

	const QColor text(t.text), muted(t.mutedText), border(t.border);
	const QColor ok(t.success), warn(t.warning);

	hits.clear();
	addHits.clear();
	removeHits.clear();
	revealRect = QRect();
	addChannelRect = QRect();

	// Header
	p.setPen(muted);
	p.drawText(QRect(0, 0, 36, headerH), Qt::AlignCenter, QStringLiteral("#"));
	p.drawText(QRect(40, 0, 52, headerH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("DEST"));
	p.drawText(QRect(120, 0, 200, headerH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("SOURCES"));
	p.setPen(QPen(withAlpha(border, 160), 1));
	p.drawLine(0, headerH, width(), headerH);

	const bool foldLine = fold.hiddenChannels > 0 || channelsExpanded;
	const int listedRows = fold.visibleRows.size() + (foldLine ? 1 : 0);

	// Line-number gutter: the number column is set off by a hairline, like a
	// terminal listing's gutter. Runs through the prompt line so the closing
	// cursor sits inside the same frame.
	const int listingBottom = headerH + (listedRows + 1) * rowH;
	p.setPen(QPen(withAlpha(border, 120), 1));
	p.drawLine(36, headerH, 36, listingBottom);

	auto drawChannelPill = [&](const QString& ch, int x, int y, int h, bool sourceSide) -> int {
		// A bare colored token, the way a terminal marks special text (ls
		// --color). Only virtual channels keep the dashed hairline frame:
		// fixed sources (IR file channels) are ports, not virtual channels.
		const bool virt = (sourceSide && portModel.fixedSourceMode()) ? false : portModel.isVirtualChannel(ch);
		const QColor ink = minimalChannelInk(ch, t.dark);
		const int w = tokenMetrics.horizontalAdvance(ch) + 12;
		const QRect pill(x, y + (rowH - h) / 2, w, h);
		if (virt)
		{
			p.setPen(QPen(withAlpha(ink, 150), 1, Qt::DashLine));
			p.setBrush(Qt::NoBrush);
			p.drawRect(pill);
		}
		p.setFont(tokenFont);
		p.setPen(ink);
		p.drawText(pill, Qt::AlignCenter, ch);
		p.setFont(mono);
		return w;
	};

	// A bracket target in the [+] grammar: 1px hairline box, muted glyph,
	// hover = exactly one background step.
	auto drawBracketTarget = [&](const QRect& rect, const QString& glyph, bool hovered) {
		p.setPen(QPen(withAlpha(border, 160), 1));
		p.setBrush(withAlpha(border, hovered ? 52 : 26));
		p.drawRect(rect);
		p.setPen(hovered ? text : muted);
		p.drawText(rect, Qt::AlignCenter, glyph);
	};

	for (int dr = 0; dr < fold.visibleRows.size(); ++dr)
	{
		const int r = fold.visibleRows[dr];
		const Assignment& a = workingAssignments[r];
		const int y = headerH + dr * rowH;
		if (dr % 2 == 1)
			p.fillRect(QRect(0, y, width(), rowH), withAlpha(border, 22));

		// Step number: a zero-padded page coordinate, the picker's console
		// numbering law applied to the listing.
		p.setPen(muted);
		p.drawText(QRect(0, y, 36, rowH), Qt::AlignCenter,
			QStringLiteral("%1").arg(dr + 1, 2, 10, QLatin1Char('0')));

		// Destination
		int x = 40;
		const QString dest = QString::fromStdWString(a.targetChannel);
		x += drawChannelPill(dest, x, y, 20, false) + 8;

		// Arrow
		p.setPen(muted);
		p.drawText(QRect(x, y, 20, rowH), Qt::AlignCenter, QStringLiteral("←"));
		x += 24;

		// Sources
		for (int si = 0; si < (int)a.sourceSum.size(); ++si)
		{
			const Assignment::Summand& s = a.sourceSum[si];
			const QString ch = QString::fromStdWString(s.channel);
			const bool neg = s.factor < 0;
			const bool showGain = s.factor != 1.0 || s.isDecibel;

			if (si > 0 || neg)
			{
				p.setPen(neg ? warn : ok);
				p.drawText(QRect(x, y, 12, rowH), Qt::AlignCenter, neg ? QStringLiteral("−") : QStringLiteral("+"));
				x += 14;
			}

			const int pillW = drawChannelPill(ch, x, y, 18, true);
			// The pill itself is an edit target so unity summands (which show no
			// gain label) can still be edited or removed via the factor editor.
			hits.append({ r, si, QRect(x, y + (rowH - 18) / 2, pillW, 18) });
			x += pillW + 4;

			if (showGain && portModel.allowFactors)
			{
				QString label;
				if (s.factor == -1.0 && !s.isDecibel)
					label = QStringLiteral("INV");
				else
				{
					const double mag = neg ? -s.factor : s.factor;
					label = s.isDecibel ? QStringLiteral("%1dB").arg(s.factor) : QStringLiteral("×%1").arg(mag);
				}
				const int w = fm.horizontalAdvance(label) + 8;
				const QRect gr(x, y + (rowH - 18) / 2, w, 18);
				p.setPen(QPen(withAlpha(border, 160), 1));
				p.setBrush(withAlpha(QColor(t.accent), 26));
				p.drawRect(gr);
				p.setPen(text);
				p.drawText(gr, Qt::AlignCenter, label);
				hits.append({ r, si, gr });
				x += w + 10;
			}
			else
			{
				x += 10;
			}

			// An open source editor is wider than the summand it replaces:
			// the rest of the step flows around it instead of vanishing
			// underneath.
			const QRect openEditor = openSummandEditorRect(r, si);
			if (!openEditor.isNull())
				x = qMax(x, openEditor.right() + 8);
		}

		// Bracketed [+] target per step: adds a source channel to this sum. This
		// is what makes an emptied Copy refillable from the GUI.
		const QRect addRect(x, y + (rowH - 18) / 2, 18, 18);
		drawBracketTarget(addRect, QStringLiteral("+"), false);
		addHits.append({ r, addRect });
		x += 22;

		// A virtual channel can leave the listing: hovering its step exposes
		// an [x] bracket target (device channels fold instead of leaving).
		if (portModel.isVirtualChannel(dest) && hoveredRow == r)
		{
			const QRect xRect(x, y + (rowH - 18) / 2, 18, 18);
			drawBracketTarget(xRect, QStringLiteral("x"), false);
			removeHits.append({ r, xRect });
		}
	}

	// The fold line: the pager's ellipsis row. The bracket target reveals the
	// folded device channels ([+N CH]) or folds the listing back ([FOLD]).
	int py = headerH + fold.visibleRows.size() * rowH;
	if (foldLine)
	{
		p.setPen(muted);
		p.drawText(QRect(0, py, 36, rowH), Qt::AlignCenter, QStringLiteral("··"));
		const QString caption = channelsExpanded
			? QStringLiteral("[FOLD]")
			: QStringLiteral("[+%1 CH]").arg(fold.hiddenChannels);
		const int w = fm.horizontalAdvance(caption) + 8;
		revealRect = QRect(44, py + (rowH - 18) / 2, w, 18);
		p.setPen(QPen(withAlpha(border, 160), 1));
		p.setBrush(withAlpha(border, hoveredControl == 1 ? 52 : 26));
		p.drawRect(revealRect);
		p.setPen(hoveredControl == 1 ? text : muted);
		p.drawText(revealRect, Qt::AlignCenter, caption);
		py += rowH;
	}

	// The session line: a prompt and a steady block cursor after the last
	// step. In Copy mode the prompt is the add-channel entry - click it and
	// type a new (virtual) channel name; in fixed-source mode it stays pure
	// staging.
	p.setPen(muted);
	p.drawText(QRect(0, py, 36, rowH), Qt::AlignCenter, QStringLiteral(">"));
	p.fillRect(QRect(44, py + (rowH - 15) / 2, 8, 15), withAlpha(QColor(t.accent), 210));
	addChannelRect = QRect(0, py, width(), rowH);
	if (hoveredControl == 2 && !channelEditorOpen())
	{
		p.setPen(withAlpha(muted, 190));
		p.drawText(QRect(58, py, 220, rowH), Qt::AlignLeft | Qt::AlignVCenter,
			QStringLiteral("add channel"));
	}
}

int StepListView::slotAt(const QPoint& pos) const
{
	if (pos.y() < headerH)
		return -1;
	return (pos.y() - headerH) / rowH;
}

QRect StepListView::summandEditorRect(const QRect& span, int minimumWidth) const
{
	// The editor spans the summand's token and gain label, and never less
	// than a full "-0.000dB*LFE": the field must show what it holds. It
	// stays inside the view's width, sliding left when the step is long.
	const int w = qMax(span.width() + 12, minimumWidth);
	int x = span.isNull() ? 40 : span.left() - 6;
	if (x + w > width())
		x = qMax(0, width() - w);
	const int y = span.isNull() ? headerH + 2 : span.center().y() - (rowH - 4) / 2;
	return QRect(x, y, w, rowH - 4);
}

QRect StepListView::channelEditorRect() const
{
	// The add-channel field opens at the prompt line's cursor.
	const int py = addChannelRect.isNull()
		? headerH + fold.visibleRows.size() * rowH
		: addChannelRect.top();
	return QRect(44, py + 2, 160, rowH - 4);
}

RoutingView* StepListRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new StepListView(assignments, channelNames, portModel, parent, tokens);
}
