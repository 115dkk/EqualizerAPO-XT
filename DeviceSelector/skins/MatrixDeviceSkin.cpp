/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix device selector: the operator's target-acquisition board.
	Constitution (colour rationing, crisp-rule/AA law, corner language):
	docs/skins/matrix.md. Element mapping: a vertical bus trace runs down
	each section's left lane; a device row's toggle is a square port node on
	that trace (hollow = idle, LED-filled with an energized trace segment =
	patched in), designated P0../C0.. under the section masthead's BUS
	letter. Hover is target acquisition (corner brackets + scanline tint);
	selection locks the target (brackets held solid + a LOCK cell);
	unavailable rows are lost signals (dashed trace stub, OFFLINE tag - the
	row stays posted). Buttons are angular cut-corner plates (the primary
	carries an EXECUTE chevron); the troubleshooting disclosure is a
	"> ..." console fold line.
*/

#include "DeviceSkinPainter.h"

#include <QFont>
#include <QPainterPath>
#include <QPolygonF>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// Board metrics: the lane sits on the Editor board's 24px pitch
// (docs/skins/matrix.md invariant 3); the toggle lane is two grid columns.
namespace BoardMetrics
{
// Width of the port lane at the left end of a device row (the toggle's
// whole hit area; >= 44 by contract, = 2 * gridPitch).
constexpr int portLaneWidth = 48;
// X of the vertical bus trace inside the row rect (half a grid column).
constexpr int traceX = 12;
// Side of the square port node cell sitting on the trace.
constexpr int nodeSide = 13;
// Corner bracket arm length of the acquisition frame.
constexpr int bracketArm = 9;
// Chamfer depth of the cut-corner button plates.
constexpr int plateCut = 8;
}

// Port designation in the board's coordinate grammar: playback ports P0..,
// capture ports C0.. - the letter names the bus, the number the seat.
QString portDesignation(const DeviceRowState& row)
{
	return (row.input ? QStringLiteral("C") : QStringLiteral("P")) + QString::number(row.index);
}

// The dialog font's point size, guarded for pixel-sized fonts.
double basePointSize(const QFont& font)
{
	const double pt = font.pointSizeF();
	return pt > 0.0 ? pt : 9.0;
}

// Four crisp L marks closing in on a target frame (AA off, integer grid).
void paintAcquisitionBrackets(QPainter& painter, const QRect& frame, const QColor& ink)
{
	painter.setPen(QPen(ink, 1));
	const int arm = qMin(BoardMetrics::bracketArm, qMin(frame.width(), frame.height()) / 3);
	if (arm < 2)
		return;
	painter.drawLine(frame.left(), frame.top(), frame.left() + arm, frame.top());
	painter.drawLine(frame.left(), frame.top(), frame.left(), frame.top() + arm);
	painter.drawLine(frame.right() - arm, frame.top(), frame.right(), frame.top());
	painter.drawLine(frame.right(), frame.top(), frame.right(), frame.top() + arm);
	painter.drawLine(frame.left(), frame.bottom() - arm, frame.left(), frame.bottom());
	painter.drawLine(frame.left(), frame.bottom(), frame.left() + arm, frame.bottom());
	painter.drawLine(frame.right(), frame.bottom() - arm, frame.right(), frame.bottom());
	painter.drawLine(frame.right() - arm, frame.bottom(), frame.right(), frame.bottom());
}

// One telemetry tag cell (LOCK / OFFLINE / EXP / DEFAULT), painted right to
// left on the names line. Returns the next free right edge.
int paintTagCell(QPainter& painter, int right, int centerY, const QString& caption,
	const QColor& ink, const QColor& rule, const QBrush& fill, Qt::PenStyle ruleStyle,
	const QString& monoFamily)
{
	QFont tagFont(monoFamily);
	tagFont.setPointSizeF(6.5);
	tagFont.setWeight(QFont::DemiBold);
	tagFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
	const QFontMetrics metrics(tagFont);
	const int cellWidth = metrics.horizontalAdvance(caption) + 9;
	const int cellHeight = metrics.height() + 3;
	const QRect cell(right - cellWidth, centerY - cellHeight / 2, cellWidth, cellHeight);
	painter.setPen(QPen(rule, 1, ruleStyle));
	painter.setBrush(fill);
	painter.drawRect(cell.adjusted(0, 0, -1, -1));
	painter.setFont(tagFont);
	painter.setPen(ink);
	painter.drawText(cell, Qt::AlignCenter, caption);
	return cell.left() - 6;
}

class MatrixDeviceSkin : public DeviceSkinPainter
{
public:
	int rowHeight(const QFontMetrics& fm, bool section) const override
	{
		// Two telemetry lines for a device, one masthead line for a section.
		// Heights derive from the dialog's font metrics (Korean-safe) and
		// round up onto the board's 4px sub-grid.
		int h = section ? fm.height() + 16 : fm.height() * 2 + 20;
		h += (4 - h % 4) % 4;
		return h;
	}

	QRect toggleRect(const QRect& rowRect) const override
	{
		// The whole port lane answers as the toggle (two grid columns).
		return QRect(rowRect.left(), rowRect.top(), BoardMetrics::portLaneWidth, rowRect.height());
	}

	void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& state, const SkinTokens& tokens) const override
	{
		if (rect.width() <= 4 || rect.height() <= 4)
			return;

		painter.save();
		// Crisp board rules: straight 1px geometry is drawn with AA off on
		// integer coordinates (constitution rule 7); text stays antialiased.
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		if (state.section)
			paintSectionRow(painter, rect, state, tokens);
		else
			paintDeviceRow(painter, rect, state, tokens);

		painter.restore();
	}

	QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const override
	{
		// Room for the chamfers plus the primary's EXECUTE chevron.
		return QSize(fm.horizontalAdvance(text) + 56, qMax(36, fm.height() + 18));
	}

	void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& state, const SkinTokens& tokens) const override
	{
		painter.save();
		// The plate's chamfer edges are genuinely diagonal - AA on for them.
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const QColor accent(tokens.accent);
		const QColor borderInk(tokens.border);
		const QColor textInk(tokens.text);
		const QColor mutedInk(tokens.mutedText);

		const QRectF plateRect = QRectF(rect).adjusted(1.5, 1.5, -1.5, -1.5);
		const double cut = qMin<double>(BoardMetrics::plateCut, plateRect.height() / 3.0);
		// Angular cut-corner plate: top-left and bottom-right chamfered.
		auto plate = [cut](const QRectF& r) {
			QPolygonF polygon;
			polygon << QPointF(r.left() + cut, r.top())
					<< QPointF(r.right(), r.top())
					<< QPointF(r.right(), r.bottom() - cut)
					<< QPointF(r.right() - cut, r.bottom())
					<< QPointF(r.left(), r.bottom())
					<< QPointF(r.left(), r.top() + cut);
			return polygon;
		};

		QColor ink(textInk);
		if (!state.enabled)
		{
			// Cancelled departure: dashed rule, empty plate, dimmed ink.
			painter.setPen(QPen(withAlpha(borderInk, 200), 1, Qt::DashLine));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolygon(plate(plateRect));
			ink = withAlpha(mutedInk, 140);
		}
		else if (state.primary)
		{
			// The EXECUTE plate: an engaged LED cell. Hover energizes the
			// edge toward ink and echoes an inner contour.
			QColor fill(accent);
			if (state.hover > 0.0)
				fill = mixColor(fill, textInk, 0.14 * state.hover);
			if (state.pressed)
				fill = mixColor(fill, QColor(tokens.background), 0.18);
			painter.setPen(QPen(mixColor(accent, textInk, 0.45 * state.hover), 1));
			painter.setBrush(fill);
			painter.drawPolygon(plate(plateRect));
			ink = skinColorIsDark(accent) ? QColor(tokens.surface) : QColor(tokens.background);
			if (state.hover > 0.0)
			{
				painter.setBrush(Qt::NoBrush);
				painter.setPen(QPen(withAlpha(ink, qRound(80.0 * state.hover)), 1));
				painter.drawPolygon(plate(plateRect.adjusted(3, 3, -3, -3)));
			}
		}
		else
		{
			// Secondary: a sunken plate whose edge pre-lights in accent.
			QColor fill(tokens.surfaceSunken);
			if (state.pressed)
				fill = mixColor(fill, accent, 0.22);
			painter.setPen(QPen(mixColor(borderInk, accent, state.pressed ? 1.0 : state.hover), 1));
			painter.setBrush(fill);
			painter.drawPolygon(plate(plateRect));
			if (state.hover > 0.0)
			{
				painter.setBrush(Qt::NoBrush);
				painter.setPen(QPen(withAlpha(accent, qRound(90.0 * state.hover)), 1));
				painter.drawPolygon(plate(plateRect.adjusted(3, 3, -3, -3)));
			}
		}

		QFont captionFont(tokens.fontFamily);
		captionFont.setPointSizeF(basePointSize(painter.font()));
		captionFont.setWeight(QFont::DemiBold);
		const QFontMetrics captionMetrics(captionFont);
		painter.setFont(captionFont);
		const QString caption = captionMetrics.elidedText(state.text, Qt::ElideRight, rect.width() - 26);
		if (state.primary)
		{
			// EXECUTE chevron ahead of the caption, nudging toward it as the
			// plate heats up.
			const int chevronWidth = 7;
			const int gap = 7;
			const int captionWidth = captionMetrics.horizontalAdvance(caption);
			const int startX = rect.center().x() - (chevronWidth + gap + captionWidth) / 2;
			const double nudge = state.enabled ? 2.0 * state.hover : 0.0;
			const double midY = QRectF(rect).center().y();
			QPainterPath chevron;
			chevron.moveTo(startX + nudge, midY - 4.0);
			chevron.lineTo(startX + nudge + chevronWidth - 2.0, midY);
			chevron.lineTo(startX + nudge, midY + 4.0);
			painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPath(chevron);
			painter.setPen(ink);
			painter.drawText(QRect(startX + chevronWidth + gap, rect.top(), captionWidth, rect.height()),
				Qt::AlignVCenter | Qt::AlignLeft, caption);
		}
		else
		{
			painter.setPen(ink);
			painter.drawText(rect, Qt::AlignCenter, caption);
		}

		// Keyboard focus: square corner brackets, the board's focus grammar.
		if (state.focused && state.enabled)
		{
			painter.setRenderHint(QPainter::Antialiasing, false);
			paintAcquisitionBrackets(painter, rect.adjusted(0, 0, -1, -1), accent);
		}
		painter.restore();
	}

	void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& state, const SkinTokens& tokens) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const QColor accent(tokens.accent);
		const QColor textInk(tokens.text);
		const QColor mutedInk(tokens.mutedText);

		// Scan tint while the cursor is on the console line.
		if (state.hover > 0.0)
			painter.fillRect(rect, withAlpha(accent, qRound(8.0 * state.hover)));

		// Console prompt chevron: points right while folded, down while open.
		const int midY = rect.center().y();
		const QPointF promptCenter(rect.left() + 14, midY);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(state.open ? accent : mixColor(mutedInk, accent, state.hover),
			1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		QPainterPath prompt;
		if (state.open)
		{
			prompt.moveTo(promptCenter.x() - 5.0, promptCenter.y() - 2.5);
			prompt.lineTo(promptCenter.x(), promptCenter.y() + 3.5);
			prompt.lineTo(promptCenter.x() + 5.0, promptCenter.y() - 2.5);
		}
		else
		{
			prompt.moveTo(promptCenter.x() - 2.5, promptCenter.y() - 5.0);
			prompt.lineTo(promptCenter.x() + 3.5, promptCenter.y());
			prompt.lineTo(promptCenter.x() - 2.5, promptCenter.y() + 5.0);
		}
		painter.drawPath(prompt);
		painter.setRenderHint(QPainter::Antialiasing, false);

		// The fold's title as a mono console line.
		QFont titleFont(tokens.monoFontFamily);
		titleFont.setPointSizeF(basePointSize(painter.font()) * 0.95);
		titleFont.setWeight(QFont::DemiBold);
		titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
		const QFontMetrics titleMetrics(titleFont);
		painter.setFont(titleFont);
		const int titleX = rect.left() + 28;
		const int caretWidth = qMax(4, titleMetrics.horizontalAdvance(QStringLiteral("M")) / 2);
		const int titleAvail = rect.right() - 10 - titleX - caretWidth - 18;
		const QString title = titleMetrics.elidedText(state.title, Qt::ElideRight, qMax(0, titleAvail));
		painter.setPen(state.open ? textInk : mixColor(mutedInk, textInk, state.hover));
		painter.drawText(QRect(titleX, rect.top(), qMax(0, titleAvail), rect.height()),
			Qt::AlignVCenter | Qt::AlignLeft, title);
		const int titleWidth = titleMetrics.horizontalAdvance(title);

		// The caret block: materializes with hover, holds steady while the
		// console line is the active (open) one.
		const double caretAlpha = qMax(state.open ? 0.85 : 0.0, 0.9 * state.hover);
		const int caretX = titleX + titleWidth + 7;
		if (caretAlpha > 0.0)
		{
			painter.fillRect(QRect(caretX, midY - titleMetrics.ascent() / 2, caretWidth, titleMetrics.ascent()),
				withAlphaF(accent, caretAlpha));
		}

		// The fold rule closing the console line toward the right edge.
		painter.setPen(QPen(withAlpha(mixColor(QColor(tokens.border), accent, 0.35 * state.hover), 150), 1));
		painter.drawLine(caretX + caretWidth + 10, midY, rect.right() - 8, midY);
		painter.restore();
	}

private:
	// Section masthead: a fold cell, the localized bus title and the BUS
	// designation readout, closed by the board's masthead rule.
	static void paintSectionRow(QPainter& painter, const QRect& rect, const DeviceRowState& state, const SkinTokens& tokens)
	{
		const QColor accent(tokens.accent);
		const QColor textInk(tokens.text);
		const QColor mutedInk(tokens.mutedText);
		const QColor borderInk(tokens.border);

		// The masthead is addressable (a click folds the bus): pre-light.
		if (state.hover > 0.0)
			painter.fillRect(rect, withAlpha(accent, qRound(10.0 * state.hover)));

		// Fold cell: a sunken square holding a drawn minus (expanded) or
		// plus (folded) - crisp lines, not glyphs.
		const int midY = rect.center().y();
		const QRect foldCell(rect.left() + 14, midY - 8, 16, 16);
		painter.setPen(QPen(mixColor(borderInk, accent, state.hover), 1));
		painter.setBrush(QColor(tokens.surfaceSunken));
		painter.drawRect(foldCell.adjusted(0, 0, -1, -1));
		painter.setPen(QPen(mixColor(textInk, accent, state.hover), 1));
		painter.drawLine(foldCell.left() + 4, midY, foldCell.right() - 5, midY);
		if (!state.expanded)
			painter.drawLine(foldCell.center().x(), foldCell.top() + 4, foldCell.center().x(), foldCell.bottom() - 5);

		// Bus designation readout on the right: the letter the port nodes
		// below inherit (P0.. ride BUS P, C0.. ride BUS C).
		QFont busFont(tokens.monoFontFamily);
		busFont.setPointSizeF(7.5);
		busFont.setWeight(QFont::DemiBold);
		busFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		const QFontMetrics busMetrics(busFont);
		const QString busCaption = state.input ? QStringLiteral("BUS C") : QStringLiteral("BUS P");
		const int busWidth = busMetrics.horizontalAdvance(busCaption);
		painter.setFont(busFont);
		painter.setPen(mutedInk);
		painter.drawText(QRect(rect.right() - 10 - busWidth, rect.top(), busWidth, rect.height()),
			Qt::AlignVCenter | Qt::AlignLeft, busCaption);

		// Localized section title as a mono board caption.
		QFont titleFont(tokens.monoFontFamily);
		titleFont.setPointSizeF(basePointSize(painter.font()) * 0.95);
		titleFont.setWeight(QFont::DemiBold);
		titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
		const QFontMetrics titleMetrics(titleFont);
		painter.setFont(titleFont);
		painter.setPen(textInk);
		const int titleX = foldCell.right() + 11;
		const int titleAvail = rect.right() - 10 - busWidth - 12 - titleX;
		painter.drawText(QRect(titleX, rect.top(), qMax(0, titleAvail), rect.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			titleMetrics.elidedText(state.connection.toUpper(), Qt::ElideRight, qMax(0, titleAvail)));

		// Masthead rule closing the section strip.
		painter.setPen(QPen(withAlpha(borderInk, 220), 1));
		painter.drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());
	}

	// One device row: bus trace + port node toggle in the port lane, two
	// telemetry lines, tag cells, and the acquisition brackets on top.
	static void paintDeviceRow(QPainter& painter, const QRect& rect, const DeviceRowState& state, const SkinTokens& tokens)
	{
		const QColor accent(tokens.accent);
		const QColor textInk(tokens.text);
		const QColor mutedInk(tokens.mutedText);
		const QColor borderInk(tokens.border);
		// Lost signals stay posted but their telemetry dims.
		const double dim = state.unavailable ? 0.55 : 1.0;

		// Locked target ground: the troubleshooting selection must stay
		// obvious under everything painted after it.
		if (state.selected)
			painter.fillRect(rect, QColor(tokens.cardSelected));

		// Target acquisition tint: a faint accent band swept by scanlines.
		if (state.hover > 0.0)
		{
			painter.fillRect(rect, withAlpha(accent, qRound(10.0 * state.hover * dim)));
			painter.setPen(QPen(withAlpha(accent, qRound(14.0 * state.hover * dim)), 1));
			for (int y = rect.top() + 2; y <= rect.bottom() - 1; y += 4)
				painter.drawLine(rect.left() + 1, y, rect.right() - 1, y);
		}

		// Cell rule between board rows.
		painter.setPen(QPen(withAlpha(borderInk, 110), 1));
		painter.drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());

		// The bus trace: an energized 2px patch cable while the port is
		// patched in (the picker's patch-trace weight), muted and
		// preheating with hover while idle, a dashed stub when the signal
		// is lost.
		const int traceX = rect.left() + BoardMetrics::traceX;
		if (state.unavailable)
			painter.setPen(QPen(withAlpha(mutedInk, 90), 1, Qt::DashLine));
		else if (state.checked)
			painter.setPen(QPen(accent, 2));
		else
			painter.setPen(QPen(withAlpha(mixColor(mutedInk, accent, 0.45 * state.hover), 110 + qRound(70.0 * state.hover)), 1));
		painter.drawLine(traceX, rect.top(), traceX, rect.bottom());

		// The port node: this row's toggle, sitting on the trace. Hollow =
		// idle port, LED fill = engaged, half fill = held down (engaging).
		const int midY = rect.center().y();
		const QRect node(traceX - BoardMetrics::nodeSide / 2, midY - BoardMetrics::nodeSide / 2,
			BoardMetrics::nodeSide, BoardMetrics::nodeSide);
		QPen nodePen(state.checked || state.pressed
			? QPen(accent, 1)
			: QPen(mixColor(mutedInk, accent, 0.65 * state.hover), 1));
		if (state.unavailable && !state.checked && !state.pressed)
			nodePen = QPen(withAlpha(mutedInk, 140), 1, Qt::DashLine);
		painter.setPen(nodePen);
		if (state.pressed)
			painter.setBrush(withAlpha(accent, 150));
		else if (state.checked)
			painter.setBrush(accent);
		else
			painter.setBrush(QColor(tokens.surfaceSunken));
		painter.drawRect(node.adjusted(0, 0, -1, -1));

		// Port designation next to the node (the bus coordinate grammar).
		QFont idFont(tokens.monoFontFamily);
		idFont.setPointSizeF(7.5);
		idFont.setBold(true);
		painter.setFont(idFont);
		if (state.checked)
			painter.setPen(accent);
		else if (state.unavailable)
			painter.setPen(withAlpha(mutedInk, 150));
		else
			painter.setPen(mixColor(mutedInk, accent, 0.65 * state.hover));
		painter.drawText(QRect(node.right() + 6, rect.top(), rect.left() + BoardMetrics::portLaneWidth - node.right() - 6, rect.height()),
			Qt::AlignVCenter | Qt::AlignLeft, portDesignation(state));

		// Two telemetry lines right of the port lane.
		const int textLeft = rect.left() + BoardMetrics::portLaneWidth + 8;
		QFont nameFont(tokens.fontFamily);
		nameFont.setPointSizeF(basePointSize(painter.font()));
		nameFont.setWeight(QFont::DemiBold);
		QFont deviceFont(tokens.monoFontFamily);
		deviceFont.setPointSizeF(basePointSize(painter.font()) * 0.9);
		QFont statusFont(tokens.monoFontFamily);
		statusFont.setPointSizeF(basePointSize(painter.font()) * 0.88);
		const QFontMetrics nameMetrics(nameFont);
		const QFontMetrics deviceMetrics(deviceFont);
		const QFontMetrics statusMetrics(statusFont);
		const int lineGap = 3;
		const int namesHeight = qMax(nameMetrics.height(), deviceMetrics.height());
		const int statusHeight = statusMetrics.height();
		const int innerTop = qMax(rect.top() + 2,
			rect.top() + (rect.height() - (namesHeight + lineGap + statusHeight)) / 2);

		// Telemetry tag cells, right to left, on the names line: LOCK
		// (accent = locked target), OFFLINE (dashed muted = lost signal),
		// EXP (hollow amber = caution), DEFAULT (plain board fact).
		int tagRight = rect.right() - 10;
		const int namesMidY = innerTop + namesHeight / 2;
		if (state.selected)
		{
			const QColor lockInk = skinColorIsDark(accent) ? QColor(tokens.surface) : QColor(tokens.background);
			tagRight = paintTagCell(painter, tagRight, namesMidY, QStringLiteral("LOCK"),
				lockInk, accent, QBrush(accent), Qt::SolidLine, tokens.monoFontFamily);
		}
		if (state.unavailable)
		{
			tagRight = paintTagCell(painter, tagRight, namesMidY, QStringLiteral("OFFLINE"),
				withAlpha(mutedInk, 190), withAlpha(mutedInk, 150), Qt::NoBrush, Qt::DashLine, tokens.monoFontFamily);
		}
		if (state.defaultDevice)
		{
			tagRight = paintTagCell(painter, tagRight, namesMidY, QStringLiteral("DEFAULT"),
				withAlphaF(textInk, 0.9 * dim + 0.1), borderInk, Qt::NoBrush, Qt::SolidLine, tokens.monoFontFamily);
		}

		// Names line: connection label in board sans, device name as a
		// muted mono readout after it.
		const int namesAvail = tagRight - textLeft;
		painter.setFont(nameFont);
		painter.setPen(state.unavailable ? withAlpha(mutedInk, 190) : textInk);
		const QString connection = nameMetrics.elidedText(state.connection, Qt::ElideRight, qMax(0, namesAvail));
		painter.drawText(QRect(textLeft, innerTop, qMax(0, namesAvail), namesHeight),
			Qt::AlignVCenter | Qt::AlignLeft, connection);
		const int deviceX = textLeft + nameMetrics.horizontalAdvance(connection) + 10;
		if (tagRight - deviceX > 24 && !state.device.isEmpty())
		{
			painter.setFont(deviceFont);
			painter.setPen(withAlpha(mutedInk, qRound(255.0 * (0.55 + 0.45 * dim))));
			painter.drawText(QRect(deviceX, innerTop, tagRight - deviceX, namesHeight),
				Qt::AlignVCenter | Qt::AlignLeft,
				deviceMetrics.elidedText(state.device, Qt::ElideRight, tagRight - deviceX));
		}

		// Status line: "> " marker + the localized status sentence. Colour
		// is rationed: amber = pending modification (install/uninstall on
		// apply), green = live and staying, muted = plain telemetry.
		QColor statusInk(mutedInk);
		if (state.unavailable)
			statusInk = withAlpha(mutedInk, 170);
		else if (state.checked != state.installed)
			statusInk = QColor(tokens.warning);
		else if (state.installed)
			statusInk = QColor(tokens.success);
		painter.setFont(statusFont);
		painter.setPen(statusInk);
		const QString marker = QStringLiteral("> ");
		const int statusTop = innerTop + namesHeight + lineGap;
		painter.drawText(QRect(textLeft, statusTop, qMax(0, namesAvail), statusHeight),
			Qt::AlignVCenter | Qt::AlignLeft, marker);
		const int statusX = textLeft + statusMetrics.horizontalAdvance(marker);
		const int statusAvail = rect.right() - 10 - statusX;
		painter.drawText(QRect(statusX, statusTop, qMax(0, statusAvail), statusHeight),
			Qt::AlignVCenter | Qt::AlignLeft,
			statusMetrics.elidedText(state.state, Qt::ElideRight, qMax(0, statusAvail)));

		// Target acquisition frame on top of everything: corner brackets
		// close in with hover; a locked (selected) target holds them solid.
		const double lockProgress = state.selected ? 1.0 : state.hover;
		if (lockProgress > 0.0)
		{
			const int inset = 2 + qRound(7.0 * (1.0 - lockProgress));
			const int alpha = state.selected ? 255 : qRound(220.0 * state.hover);
			const QRect frame = rect.adjusted(inset, inset, -inset, -inset);
			if (frame.width() > 8 && frame.height() > 8)
				paintAcquisitionBrackets(painter, frame, withAlpha(accent, qRound(alpha * (0.6 + 0.4 * dim))));
		}
	}
};
}

const DeviceSkinPainter* matrixDeviceSkinPainter()
{
	static MatrixDeviceSkin painter;
	return &painter;
}
