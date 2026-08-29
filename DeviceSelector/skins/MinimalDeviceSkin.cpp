/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Precision Minimal device selector: a terminal's device-selection menu.
	Constitution (reverse-video cursor, type ladder): docs/skins/minimal.md.
	Element mapping: a device is a printed two-line entry - a drawn [x]/[ ]
	toggle box, a 0-padded unit number, the connection as the line's command
	word and a dim ":: status" second line. Hover sweeps a reverse-video
	block across the line one character cell at a time while a '>' prompt
	caret appears in the gutter; selection holds the full inverse block
	behind a square accent hairline. A pending install/uninstall is the
	unsaved-buffer '*' in armed accent ink; an unavailable endpoint is a
	commented-out line ('#' gutter, sunken ink). Sections are "== CAPTION =="
	divider rules with a [-]/[+] fold mark, the dialog buttons are bracketed
	commands ("[ OK ]") that invert under the same sweep, and the disclosure
	is a prompt line wearing the fold mark. Reverse video is a strict
	ground/glyph swap (tokens.text <-> tokens.surface), never a third
	colour. 1px hairlines, radius 0.
*/

#include "DeviceSkinPainter.h"

#include <QFont>
#include <QPen>
#include <QStringList>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// The two-tone ink set one paint pass works with. The normal pass sits on
// the (preheated) list ground; the reverse-video pass swaps ground and
// glyph ink wholesale, so under the sweep even the accent collapses into
// the two-tone block - exactly what a terminal's inverse attribute does.
struct TerminalInk
{
	QColor ground;
	QColor body;
	QColor muted;
	QColor accent;
};

// The ground a line rests on. The device list's QSS ground is the paper
// (@CARD@) on the light sheet and the console (@BG@) on the dark one, so
// the rows follow it seamlessly; buttons and the disclosure sit on the
// dialog's @BG@ in both modes. Hover preheats exactly one value step
// (slightly darker on paper, slightly lighter on the console).
QColor restGround(const SkinTokens& tokens, double preheat, bool onList)
{
	const bool dark = skinIsDark(tokens);
	const QColor paper(onList && !dark ? tokens.card : tokens.background);
	const QColor step(dark ? tokens.surface : tokens.cardHover);
	return mixColor(paper, step, preheat);
}

TerminalInk restInk(const SkinTokens& tokens, const QColor& ground)
{
	TerminalInk ink;
	ink.ground = ground;
	ink.body = QColor(tokens.text);
	ink.muted = QColor(tokens.mutedText);
	ink.accent = QColor(tokens.accent);
	return ink;
}

TerminalInk inverseInk(const SkinTokens& tokens)
{
	TerminalInk ink;
	// The canonical reverse video swap (@TEXT@ <-> @SURFACE@).
	ink.ground = QColor(tokens.text);
	ink.body = QColor(tokens.surface);
	ink.muted = mixColor(QColor(tokens.surface), QColor(tokens.text), 0.35);
	ink.accent = QColor(tokens.surface);
	return ink;
}

// Paints one terminal line twice: the normal pass on the preheated ground
// and the reverse-video pass clipped to the sweep block. The sweep edge is
// quantized to character cells so the block advances the way a cursor
// does - one cell at a time - instead of gliding like a highlight.
template <typename PaintLine>
void paintSweptLine(QPainter& painter, const QRect& rect, double progress, const QColor& ground,
	const SkinTokens& tokens, int cell, const PaintLine& paintLine)
{
	const TerminalInk rest = restInk(tokens, ground);
	painter.fillRect(rect, rest.ground);

	int sweep = 0;
	if (progress >= 1.0)
		sweep = rect.width();
	else if (progress > 0.0)
		sweep = qBound(0, (qRound(progress * rect.width()) / cell) * cell, rect.width());

	if (sweep < rect.width())
	{
		if (sweep > 0)
		{
			painter.save();
			painter.setClipRect(QRect(rect.left() + sweep, rect.top(), rect.width() - sweep, rect.height()), Qt::IntersectClip);
			paintLine(rest);
			painter.restore();
		}
		else
			paintLine(rest);
	}
	if (sweep > 0)
	{
		const TerminalInk inverted = inverseInk(tokens);
		const QRect block(rect.left(), rect.top(), sweep, rect.height());
		painter.save();
		painter.setClipRect(block, Qt::IntersectClip);
		painter.fillRect(block, inverted.ground);
		paintLine(inverted);
		painter.restore();
	}
}

class MinimalDeviceSkin : public DeviceSkinPainter
{
public:
	int rowHeight(const QFontMetrics& fm, bool section) const override
	{
		// Terminal density: one divider line for sections, two printed
		// lines (names + ":: status") for devices. Both derive from the
		// metrics so localized text keeps its ascenders.
		if (section)
			return fm.height() + 12;
		return fm.height() * 2 + 14;
	}

	QRect toggleRect(const QRect& rowRect) const override
	{
		// The prompt gutter plus the [x] glyph box; the whole left end of
		// the line belongs to the toggle (>= 54px, growing with the row).
		return QRect(rowRect.left(), rowRect.top(), qMax(54, rowRect.height() + 8), rowRect.height());
	}

	void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& state, const SkinTokens& tokens) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		QFont mono = painter.font();
		mono.setFamily(tokens.monoFontFamily);
		mono.setBold(false);
		QFont monoBold = mono;
		monoBold.setBold(true);
		const QFontMetrics fm(mono);
		const QFontMetrics bfm(monoBold);
		const int cw = qMax(4, fm.horizontalAdvance(QLatin1Char('0')));

		if (state.section)
		{
			// "== PLAYBACK DEVICES ==": the section's own text set as a
			// divider caption (uppercase, tracked, one hairline rule to the
			// fold mark). Clicking folds, so the line takes the sweep too.
			QFont capFont = monoBold;
			capFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
			const QFontMetrics cfm(capFont);
			const QString marker = state.expanded ? QStringLiteral("[-]") : QStringLiteral("[+]");
			const int markerW = cfm.horizontalAdvance(marker);
			const int left = rect.left() + 8;
			const int right = rect.right() - 10;
			const QString caption = QStringLiteral("== ") + state.connection.toUpper() + QStringLiteral(" ==");

			paintSweptLine(painter, rect, state.hover, restGround(tokens, state.hover, true), tokens, cw, [&](const TerminalInk& ink)
			{
				painter.setFont(capFont);
				painter.setPen(ink.muted);
				const int capMax = qMax(0, right - markerW - cw * 2 - left);
				const QString printed = cfm.elidedText(caption, Qt::ElideRight, capMax);
				painter.drawText(QRect(left, rect.top(), capMax, rect.height()),
					Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, printed);
				painter.drawText(QRect(right - markerW, rect.top(), markerW, rect.height()),
					Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip, marker);

				const int ruleStart = left + cfm.horizontalAdvance(printed) + cw;
				const int ruleEnd = right - markerW - cw;
				if (ruleEnd > ruleStart)
				{
					painter.setPen(QPen(withAlpha(ink.muted, 150), 1));
					const qreal y = rect.center().y() + 0.5;
					painter.drawLine(QPointF(ruleStart, y), QPointF(ruleEnd, y));
				}
			});
			painter.restore();
			return;
		}

		// Device entry. Column grid in character cells from the left:
		// gutter caret, [x] box, pending '*', unit number, names.
		const int lineH = fm.height();
		const int top = rect.top() + (rect.height() - (lineH * 2 + 2)) / 2;
		const QRect line1(rect.left(), top, rect.width(), lineH);
		const QRect line2(rect.left(), top + lineH + 2, rect.width(), lineH);
		const int caretX = rect.left() + 8;
		const int toggleX = caretX + cw * 2;
		const QRect glyphCell(toggleX, line1.top(), cw * 3, lineH);
		const int starX = toggleX + cw * 3;
		const int numberX = starX + cw * 2;
		const int nameX = numberX + cw * 3;
		const int right = rect.right() - 10;
		const bool pendingChange = state.checked != state.installed;

		// Right-aligned bare mono tags (stylistic captions, untranslated).
		QStringList tags;
		if (state.defaultDevice)
			tags << QStringLiteral("DEF");
		if (state.input)
			tags << QStringLiteral("REC");

		// Selection holds the full inverse block; hover sweeps toward it.
		const double progress = state.selected ? 1.0 : state.hover;

		paintSweptLine(painter, rect, progress, restGround(tokens, state.hover, true), tokens, cw, [&](const TerminalInk& ink)
		{
			const QColor primaryInk = state.unavailable ? ink.muted : ink.body;
			painter.setFont(mono);

			// Gutter: the hovered/selected line carries the prompt caret; an
			// unavailable endpoint reads as a commented-out line instead.
			if (state.unavailable)
			{
				painter.setPen(ink.muted);
				painter.drawText(QRect(caretX, line1.top(), cw * 2, lineH),
					Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, QStringLiteral("#"));
			}
			else if (state.selected || state.hover > 0.0)
			{
				painter.setFont(monoBold);
				painter.setPen(ink.accent);
				painter.drawText(QRect(caretX, line1.top(), cw * 2, lineH),
					Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, QStringLiteral(">"));
				painter.setFont(mono);
			}

			// The toggle: a drawn glyph box. Pressing inverts just the box -
			// inside the sweep block that re-inverts, like a cursor blink.
			if (state.pressed)
			{
				painter.fillRect(glyphCell.adjusted(-1, -1, 1, 1), ink.body);
				painter.setPen(ink.ground);
			}
			else
				painter.setPen(state.checked ? primaryInk : ink.muted);
			painter.setFont(state.checked ? monoBold : mono);
			painter.drawText(glyphCell, Qt::AlignCenter | Qt::TextDontClip,
				state.checked ? QStringLiteral("[x]") : QStringLiteral("[ ]"));
			painter.setFont(mono);

			// The unsaved-buffer mark: this line's state will change on OK.
			if (pendingChange)
			{
				painter.setFont(monoBold);
				painter.setPen(ink.accent);
				painter.drawText(QRect(starX, line1.top(), cw, lineH),
					Qt::AlignCenter | Qt::TextDontClip, QStringLiteral("*"));
				painter.setFont(mono);
			}

			// Unit number: the page coordinate under its section (01, 02...).
			painter.setPen(ink.muted);
			painter.drawText(QRect(numberX, line1.top(), cw * 2, lineH),
				Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
				QStringLiteral("%1").arg(state.index + 1, 2, 10, QLatin1Char('0')));

			int nameRight = right;
			if (!tags.isEmpty())
			{
				const QString tagText = tags.join(QStringLiteral("  "));
				const int tagW = fm.horizontalAdvance(tagText);
				painter.drawText(QRect(right - tagW, line1.top(), tagW, lineH),
					Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip, tagText);
				nameRight = right - tagW - cw * 2;
			}

			// Names: the connection is the line's command word (bold, body
			// ink), the device follows in plain body ink - hierarchy by
			// weight and brightness, one size (the constitution's ladder).
			painter.setFont(monoBold);
			painter.setPen(primaryInk);
			const QString conn = bfm.elidedText(state.connection, Qt::ElideRight, qMax(0, nameRight - nameX));
			painter.drawText(QRect(nameX, line1.top(), qMax(0, nameRight - nameX), lineH),
				Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, conn);
			const int devX = nameX + bfm.horizontalAdvance(conn) + cw * 2;
			if (!state.device.isEmpty() && devX < nameRight)
			{
				painter.setFont(mono);
				painter.setPen(primaryInk);
				painter.drawText(QRect(devX, line1.top(), nameRight - devX, lineH),
					Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
					fm.elidedText(state.device, Qt::ElideRight, nameRight - devX));
			}

			// The status sentence: a dim ":: " second line; a pending change
			// arms it with accent ink until OK executes.
			painter.setFont(mono);
			painter.setPen(!state.unavailable && pendingChange ? ink.accent : ink.muted);
			const int statusW = qMax(0, right - nameX);
			painter.drawText(QRect(nameX, line2.top(), statusW, lineH),
				Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
				fm.elidedText(QStringLiteral(":: ") + state.state, Qt::ElideRight, statusW));
		});

		// The held selection is framed by the square accent hairline so it
		// stays apart from a merely fully-hovered line.
		if (state.selected)
		{
			painter.setPen(QPen(QColor(tokens.accent), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
		}
		painter.restore();
	}

	QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const override
	{
		// The bracketed command word plus breathing room; the tracking the
		// paint adds stays well inside the padding.
		return QSize(fm.horizontalAdvance(QStringLiteral("[ ") + text.toUpper() + QStringLiteral(" ]")) + 28,
			qMax(32, fm.height() + 14));
	}

	void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& state, const SkinTokens& tokens) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		QFont commandFont = painter.font();
		commandFont.setFamily(tokens.monoFontFamily);
		commandFont.setBold(state.primary);
		commandFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		const QFontMetrics cfm(commandFont);
		const int cw = qMax(4, cfm.horizontalAdvance(QLatin1Char('0')));
		const QString printed = cfm.elidedText(
			QStringLiteral("[ ") + state.text.toUpper() + QStringLiteral(" ]"),
			Qt::ElideRight, qMax(0, rect.width() - 4));

		// A disabled command is engraved and sunken: bare muted type on the
		// dialog ground, no brackets removed, no box invented.
		if (!state.enabled)
		{
			painter.setFont(commandFont);
			painter.setPen(withAlpha(QColor(tokens.mutedText), 150));
			painter.drawText(rect, Qt::AlignCenter, printed);
			painter.restore();
			return;
		}

		// Hover sweeps the inverse block across the command; the press
		// instant snaps it to the full block (the picker's blunt cursor).
		const double progress = state.pressed ? 1.0 : state.hover;
		paintSweptLine(painter, rect, progress, restGround(tokens, state.hover, false), tokens, cw, [&](const TerminalInk& ink)
		{
			painter.setFont(commandFont);
			// The primary command is body ink and bold; the secondary rests
			// muted and lifts to body brightness as the pointer arrives.
			QColor pen = state.primary ? ink.body : ink.muted;
			if (state.hover > 0.0)
				pen = mixColor(pen, ink.body, state.hover);
			painter.setPen(pen);
			painter.drawText(rect, Qt::AlignCenter, printed);
		});

		if (state.focused && !state.pressed)
		{
			painter.setPen(QPen(QColor(tokens.focusRing), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
		}
		painter.restore();
	}

	void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& state, const SkinTokens& tokens) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		QFont mono = painter.font();
		mono.setFamily(tokens.monoFontFamily);
		mono.setBold(false);
		QFont monoBold = mono;
		monoBold.setBold(true);
		const QFontMetrics fm(mono);
		const QFontMetrics bfm(monoBold);
		const int cw = qMax(4, fm.horizontalAdvance(QLatin1Char('0')));
		const QString marker = state.open ? QStringLiteral("[-]") : QStringLiteral("[+]");
		const int left = rect.left() + 8;
		const int right = rect.right() - 10;

		// The prompt line: fold mark, title, and a hairline rule closing the
		// strip - the section divider's grammar wearing the fold bracket.
		paintSweptLine(painter, rect, state.hover, restGround(tokens, state.hover, false), tokens, cw, [&](const TerminalInk& ink)
		{
			painter.setFont(monoBold);
			painter.setPen(state.open ? ink.body : mixColor(ink.muted, ink.body, state.hover));
			painter.drawText(QRect(left, rect.top(), bfm.horizontalAdvance(marker) + cw, rect.height()),
				Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, marker);

			painter.setFont(mono);
			painter.setPen(state.open ? ink.body : mixColor(ink.muted, ink.body, state.hover));
			const int titleX = left + bfm.horizontalAdvance(marker) + cw * 2;
			const int titleMax = qMax(0, right - titleX - cw * 3);
			const QString printed = fm.elidedText(state.title, Qt::ElideRight, titleMax);
			painter.drawText(QRect(titleX, rect.top(), titleMax, rect.height()),
				Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, printed);

			const int ruleStart = titleX + fm.horizontalAdvance(printed) + cw;
			if (right > ruleStart)
			{
				painter.setPen(QPen(withAlpha(ink.muted, 150), 1));
				const qreal y = rect.center().y() + 0.5;
				painter.drawLine(QPointF(ruleStart, y), QPointF(right, y));
			}
		});
		painter.restore();
	}
};
}

const DeviceSkinPainter* minimalDeviceSkinPainter()
{
	static MinimalDeviceSkin painter;
	return &painter;
}
