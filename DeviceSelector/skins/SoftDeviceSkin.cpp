/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Soft device selector: an ordinary device list. Constitution:
	docs/skins/soft.md. Element mapping:

	- A section ("Playback devices") is a heading: a fold chevron, a small
	  speaker or microphone glyph, the title. No pill, no fill.
	- A device is a card like the Editor's rows: the card surface inside a
	  1px line. Its toggle is a check box - the control every installer uses
	  for "apply this here". The name leads; the status sentence under it
	  starts with one lamp: success for "installed and staying", the
	  "needs a look" colour for a pending change, a hollow ring for "nothing
	  here".
	- Hover takes the card's line one step heavier; the card chosen as the
	  troubleshooting target has its line in the emphasis colour. Nothing is
	  tinted and nothing lifts.
	- An unavailable endpoint only fades.
	- Buttons are ordinary buttons; the accept button is the dialog's one
	  filled action. The troubleshooting disclosure is a chevron and a title.
*/

#include "DeviceSkinPainter.h"

#include <QFont>
#include <QPainterPath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/soft/SoftPaint.h"

namespace
{
QPainterPath checkPath(const QRectF& box)
{
	QPainterPath path;
	path.moveTo(box.left() + box.width() * 0.24, box.top() + box.height() * 0.53);
	path.lineTo(box.left() + box.width() * 0.43, box.top() + box.height() * 0.71);
	path.lineTo(box.left() + box.width() * 0.77, box.top() + box.height() * 0.30);
	return path;
}

// Playback section glyph: a small rounded speaker with one sound arc.
void drawSpeakerGlyph(QPainter& painter, const QPointF& c, qreal s, const QColor& ink)
{
	painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	QPainterPath body;
	body.moveTo(c.x() - s * 0.50, c.y() - s * 0.22);
	body.lineTo(c.x() - s * 0.18, c.y() - s * 0.22);
	body.lineTo(c.x() + s * 0.12, c.y() - s * 0.50);
	body.lineTo(c.x() + s * 0.12, c.y() + s * 0.50);
	body.lineTo(c.x() - s * 0.18, c.y() + s * 0.22);
	body.lineTo(c.x() - s * 0.50, c.y() + s * 0.22);
	body.closeSubpath();
	painter.drawPath(body);
	const QRectF wave(c.x() - s * 0.02, c.y() - s * 0.30, s * 0.52, s * 0.60);
	painter.drawArc(wave, -55 * 16, 110 * 16);
}

// Capture section glyph: a rounded microphone capsule on its cradle.
void drawMicGlyph(QPainter& painter, const QPointF& c, qreal s, const QColor& ink)
{
	painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	const QRectF capsule(c.x() - s * 0.15, c.y() - s * 0.50, s * 0.30, s * 0.60);
	painter.drawRoundedRect(capsule, s * 0.15, s * 0.15);
	const QRectF cradle(c.x() - s * 0.33, c.y() - s * 0.30, s * 0.66, s * 0.64);
	painter.drawArc(cradle, 180 * 16, -180 * 16);
	painter.drawLine(QPointF(c.x(), c.y() + s * 0.34), QPointF(c.x(), c.y() + s * 0.50));
}

// A section heading. The whole row folds the section, so the pointer gets
// the neutral step behind it.
void paintSection(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t)
{
	const QRectF row = QRectF(rect).adjusted(2.0, 3.0, -2.0, -3.0);
	if (s.hover > 0.0)
		SoftPaint::box(painter, row, withAlphaF(QColor(t.cardHover), s.hover), QColor(), SoftPaint::controlRadius, 0.0);

	const QColor muted(t.mutedText);
	SoftPaint::chevron(painter, QPointF(row.left() + 12.0, row.center().y()), muted, s.expanded, 3.5);
	const QPointF glyphCenter(row.left() + 34.0, row.center().y());
	if (s.input)
		drawMicGlyph(painter, glyphCenter, 15.0, muted);
	else
		drawSpeakerGlyph(painter, glyphCenter, 15.0, muted);

	const QFont titleFont = SoftPaint::bodyFont(t, 14, QFont::DemiBold);
	const QFontMetrics titleFm(titleFont);
	const int textLeft = int(row.left()) + 50;
	const int titleWidth = int(row.right()) - textLeft - 8;
	painter.setFont(titleFont);
	painter.setPen(QColor(t.text));
	painter.drawText(QRect(textLeft, int(row.top()), titleWidth, int(row.height())),
		Qt::AlignVCenter | Qt::AlignLeft, titleFm.elidedText(s.connection, Qt::ElideRight, titleWidth));
}
}

namespace
{
class SoftDeviceSkin : public DeviceSkinPainter
{
public:
	int rowHeight(const QFontMetrics& fm, bool section) const override
	{
		if (section)
			return fm.height() + 20;
		// Two lines of text and the card's own margins.
		return fm.height() * 2 + 30;
	}

	QRect toggleRect(const QRect& rowRect) const override
	{
		// The left end of the card: the check box and the room round it.
		return QRect(rowRect.left(), rowRect.top(), 56, rowRect.height());
	}

	void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		if (s.section)
			paintSection(painter, rect, s, t);
		else
			paintDeviceCard(painter, rect, s, t);
		painter.restore();
	}

	QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const override
	{
		return QSize(qMax(88, fm.horizontalAdvance(text) + 40), qMax(36, fm.height() + 16));
	}

	void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const QRectF box = QRectF(rect).adjusted(2.0, 2.0, -2.0, -2.0);
		const QColor accent(t.accent);
		const QColor onAccent(t.onAccent);
		QColor fill;
		QColor edge;
		QColor ink(t.text);
		qreal edgeWidth = 1.0;
		if (s.primary && s.enabled)
		{
			fill = s.pressed ? mixColor(accent, onAccent, 0.28) : mixColor(accent, onAccent, 0.16 * s.hover);
			edge = QColor();
			edgeWidth = 0.0;
			ink = onAccent;
			if (s.focused)
			{
				edge = QColor(t.text);
				edgeWidth = 2.0;
			}
		}
		else if (s.primary)
		{
			// The filled action that cannot be taken is an outline: a dimmed
			// fill would read as a tinted area.
			fill = QColor();
			edge = QColor(t.borderStrong);
			ink = QColor(t.mutedText);
		}
		else
		{
			if (!s.enabled)
				painter.setOpacity(SoftPaint::disabledOpacity);
			fill = s.pressed && s.enabled ? QColor(t.cardSelected)
				: mixColor(QColor(t.card), QColor(t.cardHover), s.enabled ? s.hover : 0.0);
			edge = mixColor(QColor(t.borderStrong), QColor(t.controlEdge), s.enabled ? s.hover : 0.0);
			if (s.focused && s.enabled)
			{
				edge = QColor(t.focusRing);
				edgeWidth = 2.0;
			}
		}
		SoftPaint::box(painter, box, fill, edge, SoftPaint::controlRadius, edgeWidth);

		painter.setFont(SoftPaint::bodyFont(t, 14, s.primary ? QFont::DemiBold : QFont::Medium));
		painter.setPen(ink);
		painter.drawText(box.toRect(), Qt::AlignCenter, s.text);
		painter.restore();
	}

	void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const QRectF strip = QRectF(rect).adjusted(0.0, 3.0, 0.0, -3.0);
		if (s.hover > 0.0)
			SoftPaint::box(painter, strip, withAlphaF(QColor(t.cardHover), s.hover), QColor(), SoftPaint::controlRadius, 0.0);

		SoftPaint::chevron(painter, QPointF(strip.left() + 12.0, strip.center().y()), QColor(t.mutedText), s.open, 3.5);

		const QFont titleFont = SoftPaint::bodyFont(t, 14, QFont::DemiBold);
		const QFontMetrics titleFm(titleFont);
		const int textLeft = int(strip.left()) + 26;
		const int titleWidth = rect.width() - (textLeft - rect.left()) - 10;
		painter.setFont(titleFont);
		painter.setPen(QColor(t.text));
		painter.drawText(QRect(textLeft, rect.top(), titleWidth, rect.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			titleFm.elidedText(s.title, Qt::ElideRight, titleWidth));
		painter.restore();
	}

private:
	static void paintDeviceCard(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t)
	{
		const QColor bodyInk(t.text);
		const QColor muted(t.mutedText);
		const QColor accent(t.accent);
		const double hover = s.unavailable ? 0.0 : s.hover;

		// An unavailable endpoint fades as a whole.
		if (s.unavailable)
			painter.setOpacity(SoftPaint::disabledOpacity);

		const QRectF cardRect = QRectF(rect).adjusted(4.0, 3.0, -4.0, -3.0);
		QColor edge = mixColor(QColor(t.border), QColor(t.borderStrong), hover);
		if (s.selected)
			edge = accent;
		SoftPaint::box(painter, cardRect, QColor(t.card), edge, SoftPaint::surfaceRadius);

		// The check box, at the middle of the toggle's hit area.
		QRectF check(0.0, 0.0, 18.0, 18.0);
		check.moveCenter(QPointF(cardRect.left() + 24.0, cardRect.center().y()));
		if (s.checked)
		{
			SoftPaint::box(painter, check, s.pressed ? mixColor(accent, QColor(t.onAccent), 0.28) : accent, QColor(), 4.0, 0.0);
			painter.setPen(QPen(QColor(t.onAccent), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawPath(checkPath(check));
		}
		else
		{
			SoftPaint::box(painter, check, QColor(s.pressed ? t.cardSelected : t.card),
				mixColor(QColor(t.controlEdge), bodyInk, hover), 4.0);
		}

		const QFont nameFont = SoftPaint::bodyFont(t, 14, QFont::DemiBold);
		const QFont deviceFont = SoftPaint::bodyFont(t, 13);
		const QFont statusFont = SoftPaint::bodyFont(t, 13);
		const QFontMetrics nameFm(nameFont);
		const QFontMetrics deviceFm(deviceFont);
		const QFontMetrics statusFm(statusFont);

		const qreal rightLimit = cardRect.right() - 14.0;
		const qreal textLeft = check.right() + 14.0;
		const qreal textWidth = rightLimit - textLeft;
		if (textWidth <= 24.0)
			return;

		const int lineGap = 3;
		const int blockH = nameFm.height() + lineGap + statusFm.height();
		const qreal lineTop = cardRect.center().y() - blockH / 2.0;

		// Line 1: the connection name leads; the hardware name follows in the
		// quiet ink.
		painter.setFont(nameFont);
		painter.setPen(bodyInk);
		const QString connection = nameFm.elidedText(s.connection, Qt::ElideRight, int(textWidth));
		painter.drawText(QRectF(textLeft, lineTop, textWidth, nameFm.height()),
			Qt::AlignVCenter | Qt::AlignLeft, connection);
		const qreal deviceLeft = textLeft + nameFm.horizontalAdvance(connection) + 10.0;
		if (!s.device.isEmpty() && rightLimit - deviceLeft > 24.0)
		{
			painter.setFont(deviceFont);
			painter.setPen(muted);
			painter.drawText(QRectF(deviceLeft, lineTop, rightLimit - deviceLeft, nameFm.height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				deviceFm.elidedText(s.device, Qt::ElideRight, int(rightLimit - deviceLeft)));
		}

		// Line 2: one lamp, then the status sentence. The sentence already
		// names the default device, so the card carries no second mark for it
		// (shared article 1).
		const qreal statusTop = lineTop + nameFm.height() + lineGap;
		const QPointF lampCenter(textLeft + 4.0, statusTop + statusFm.height() / 2.0 + 0.5);
		const bool pendingChange = s.checked != s.installed && !s.unavailable;
		QColor reportColor;
		const bool reported = reportInk(s, t, reportColor);
		if (reported)
			SoftPaint::lamp(painter, lampCenter, reportColor, s.report == DeviceReport::Unchecked);
		else if (pendingChange)
			SoftPaint::lamp(painter, lampCenter, QColor(t.warning));
		else if (s.checked && s.installed && !s.unavailable)
			SoftPaint::lamp(painter, lampCenter, QColor(t.success));
		else
			SoftPaint::lamp(painter, lampCenter, muted, true);

		painter.setFont(statusFont);
		painter.setPen((pendingChange || reported) && s.report != DeviceReport::Unchecked ? bodyInk : muted);
		const qreal statusLeft = textLeft + 16.0;
		painter.drawText(QRectF(statusLeft, statusTop, rightLimit - statusLeft, statusFm.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			statusFm.elidedText(s.state, Qt::ElideRight, int(rightLimit - statusLeft)));
	}
};
}

const DeviceSkinPainter* softDeviceSkinPainter()
{
	static SoftDeviceSkin painter;
	return &painter;
}
