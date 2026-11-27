/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MatrixSkin.h"

#include <QBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QWidget>
#include <QtMath>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/widgets/FilterCardModel.h"
#include "MatrixSkinDetail.h"
#include "Editor/skins/matrix/cards/MatrixReferenceCardView.h"

static void paintEvalReadout(QPainter& painter, const QRect& headerBand, const CommandRowInfo& info, const SkinTokens& tokens);

namespace
{

// Bus letter of a command type for the row coordinate ("B3"). Letters are
// designations, not identifiers - two types may share one; uniqueness stays
// with the line number.
QString matrixBusLetter(const QString& type)
{
	const FilterCardKind kind = filterCardKindFromType(type);
	if (kind == FilterCardKind::Biquad)
		return QStringLiteral("B");
	if (kind == FilterCardKind::Preamp)
		return QStringLiteral("P");
	if (kind == FilterCardKind::Delay || kind == FilterCardKind::Device)
		return QStringLiteral("D");
	if (kind == FilterCardKind::GraphicEq)
		return QStringLiteral("G");
	if (kind == FilterCardKind::Copy || kind == FilterCardKind::Channel || kind == FilterCardKind::NewChannel
		|| kind == FilterCardKind::Send || kind == FilterCardKind::Convolution)
		return QStringLiteral("C");
	if (kind == FilterCardKind::Include)
		return QStringLiteral("I");
	if (kind == FilterCardKind::Vst)
		return QStringLiteral("V");
	// If/Eval get their own designations so they stay out of the R (remark)
	// carrier.
	if (kind == FilterCardKind::If)
		return QStringLiteral("F");
	if (kind == FilterCardKind::Eval)
		return QStringLiteral("E");
	if (kind == FilterCardKind::Stage)
		return QStringLiteral("S");
	if (kind == FilterCardKind::Loudness)
		return QStringLiteral("L");
	if (kind == FilterCardKind::Comment)
		return QStringLiteral("#");
	// Unrecognized raw lines: a remark entry on the board.
	return QStringLiteral("R");
}

// Status lamp in the left gutter: solid green = active, hollow amber =
// bypassed. Gate rows post the engine's branch decision in the same lamp
// grammar, and a swallowed line un-lights its lamp. The analysis facts are
// advisory and only read here, at paint time. A remark gets no lamp (the
// caller decides).
void paintStatusLamp(QPainter& painter, const QRect& lampRect, const CommandRowInfo& info, const SkinTokens& tokens)
{
	const auto solidLamp = [&](const QColor& ink)
	{
		painter.fillRect(lampRect, ink);
	};
	const auto hollowLamp = [&](const QColor& ink)
	{
		painter.setPen(QPen(ink, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(lampRect.adjusted(0, 0, -1, -1));
	};
	if (!info.enabled)
	{
		hollowLamp(QColor(tokens.warning));
	}
	else if (info.kind() == FilterCardKind::If)
	{
		// Gate lamp: taken = solid success, condition false = hollow
		// success, evaluation fault = solid danger, unreached or not
		// yet analysed = hollow muted (no decision posted).
		if (info.branchState == 1)
			solidLamp(QColor(tokens.success));
		else if (info.branchState == 3)
			solidLamp(QColor(tokens.danger));
		else if (info.branchState == 0)
			hollowLamp(QColor(tokens.success));
		else
			hollowLamp(QColor(tokens.mutedText));
	}
	else if (info.lineSkipped)
	{
		// Cancelled departure: live source behind a closed gate keeps
		// its running lamp, unlit.
		hollowLamp(QColor(tokens.success));
	}
	else
	{
		solidLamp(QColor(tokens.success));
	}
}

// A Modern row is one cell of the board: one surface from the rail to the
// right rule. The card's header band (a lighter strip with the column grid)
// and its flooded body are two surfaces; across one line the left column
// kept the strip's plate, the editor stood on the body's ground and the
// name cell fell in the seam between them. The whole row takes the ground
// the editors already stand on, so their panels and the name cell share
// it. Like the card body, the row is an opaque panel, so the column grid
// stays on the board between the rows (invariant rule 3).
void paintRowChrome(QPainter& painter, const QRect& content, const CommandRowInfo& info, const SkinTokens& tokens)
{
	// A selected row is engaged the way a selected card's header band is:
	// the raised surface, over the whole row since the row has no band.
	painter.fillRect(content, QColor(info.selected ? tokens.surfaceRaised : tokens.background));

	const bool remark = info.kind() == FilterCardKind::Comment;
	const int band = qMin(MatrixMetrics::rowBandHeight, content.height());

	// Crosspoint hover: the row band is the whole row and the coordinate
	// column band runs down the left column; their intersection is the
	// crosspoint (invariant rule 6, the card's alphas).
	if (info.hovered && (info.enabled || remark))
	{
		QColor rowBand(tokens.accent);
		rowBand.setAlpha(22);
		painter.fillRect(content, rowBand);
		QColor crosspoint(tokens.accent);
		crosspoint.setAlpha(14);
		painter.fillRect(QRect(content.left(), content.top(),
			qMin(MatrixMetrics::coordinateBandWidth, content.width()), content.height()), crosspoint);
	}

	// The lamp stands level with the coordinate cell and the power key: on the
	// top band's centre line, or on the row's own centre where the row centres
	// its left column (Copy, GraphicEQ, IIR). headerCenterY is in the row
	// widget's coordinates, the frame sits cardMarginTop below its top.
	if (!remark)
	{
		const int lampCentre = info.headerCenterY > 0
			? qRound(info.headerCenterY) - CommandRowInfo::cardMarginTop
			: content.top() + band / 2;
		paintStatusLamp(painter, QRect(content.left() + 1, lampCentre - 3, 5, 5), info, tokens);
	}

	paintEvalReadout(painter, QRect(content.left(), content.top(), content.width(), band), info, tokens);
}

}

QString MatrixSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const bool remark = info.kind() == FilterCardKind::Comment;
	const bool cancelled = !remark && info.enabled && info.lineSkipped;
	const QString railColor = remark || cancelled ? tokens.border : (info.enabled ? tokens.success : tokens.warning);
	const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
	const QString backgroundColor = info.selected ? tokens.cardSelected : tokens.card;
	const QString borderStyle = ((info.enabled && !cancelled) || remark) ? QStringLiteral("solid") : QStringLiteral("dashed");
	if (info.inlineRow)
	{
		// A Modern row is one cell on the board, and a lit cell is ruled in one
		// colour on all four sides: the rail takes the lit colour too while the
		// row is selected, focused or under the pointer (the status lamp keeps
		// posting the running state). At rest the rail keeps its signal colour.
		const bool lit = info.focused || info.selected;
		const QString litColor = info.focused ? tokens.focusRing : tokens.accent;
		QString rowStyle = QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-left: 3px solid %4; border-radius: 0px; }")
			.arg(backgroundColor, borderStyle, lit ? litColor : tokens.border, lit ? litColor : railColor);
		rowStyle += QStringLiteral(
			" QFrame#FilterCardRow:hover { border: 1px %1 %2; border-left: 3px solid %2; }")
			.arg(borderStyle, tokens.accent);
		return rowStyle;
	}
	QString style = QStringLiteral(
		"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-left: 3px solid %4; border-radius: 0px; }")
		.arg(backgroundColor, borderStyle, borderColor, railColor);
	// The :hover rule both signals the row crosspoint and makes Qt repaint
	// the frame on enter/leave, which drives the painted column band.
	style += QStringLiteral(
		" QFrame#FilterCardRow:hover { border: 1px %1 %2; border-left: 3px solid %3; }")
		.arg(borderStyle, tokens.accent, railColor);
	return style;
}

QString MatrixSkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	Q_UNUSED(info);
	Q_UNUSED(tokens);
	return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0px; }");
}

bool MatrixSkin::supportsRowMode() const
{
	return true;
}

BadgeTreatment MatrixSkin::badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& tokens) const
{
	Q_UNUSED(typeColor);
	Q_UNUSED(badgeToken);
	// The pictogram prints on the header band; a cell around it was one
	// more closed box inside the card.
	const QString ink = info.enabled ? tokens.text : tokens.mutedText;
	return {
		QStringLiteral("color:%1; border-color:transparent; background-color:transparent;").arg(ink),
		QColor(ink)
	};
}

void MatrixSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
		const SkinTokens& tokens) const
{
	if (info.legacyRow)
		return;

	// A Modern row: the left column keeps the gutter lamp clear on its left
	// and gives the rest of the line to the editor; the editor or the channel
	// badges after it keep a small gap from the row's right rule.
	if (info.inlineRow && card != nullptr && card->layout() != nullptr)
	{
		card->layout()->setContentsMargins(0, 0, 4, 0);
		if (QBoxLayout* column = header != nullptr ? qobject_cast<QBoxLayout*>(header->layout()) : nullptr)
		{
			column->setContentsMargins(8, 0, 2, 0);
			column->setSpacing(4);
		}
		if (body != nullptr)
		{
			body->setContentsMargins(0, 0, 0, 0);
			// The reference view lays itself out for the row; its own
			// construction-time consultation never sees inlineRow.
			for (MatrixReferenceCardView* view : body->findChildren<MatrixReferenceCardView*>())
				view->setInlineRow(true);
		}
	}

	// The plain line number becomes a board coordinate: the type's bus
	// letter ahead of the stable line position ("B3"). Spacer rows are
	// blank board lines and carry no coordinate.
	if (card != nullptr && header != nullptr && info.kind() != FilterCardKind::Spacer)
	{
		QLabel* coordinateCell = header->findChild<QLabel*>(QStringLiteral("FilterCardNumber"));
		if (coordinateCell != nullptr)
		{
			bool plainNumber = false;
			const int line = coordinateCell->text().toInt(&plainNumber);
			if (plainNumber)
				coordinateCell->setText(matrixBusLetter(info.type) + QString::number(line));
		}
	}

	// Bare/unmodelled lines (TXT, the If/EndIf/Eval vocabulary) print on
	// the board as a remark line: the ">_" scan glyph in muted mono ahead
	// of the raw line in mono. The shared raw card lays inline styles on
	// its two labels, so the board answer is inline too.
	if (FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine) && body != nullptr)
	{
		if (QLabel* glyph = body->findChild<QLabel*>(QStringLiteral("FilterCardRawGlyph")))
		{
			glyph->setStyleSheet(QStringLiteral(
				"QLabel#FilterCardRawGlyph { background:transparent; color:%1; border:0;"
				" padding:2px 0; font-family:\"%2\"; font-weight:700; font-size:9pt; }")
				.arg(tokens.mutedText, tokens.monoFontFamily));
		}
		if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
		{
			raw->setStyleSheet(QStringLiteral(
				"QLabel#FilterCardRawText { background:transparent; color:%1; border:0;"
				" padding:4px 0; font-family:\"%2\"; font-size:9pt; }")
				.arg(tokens.text, tokens.monoFontFamily));
		}
	}

	// The reference bodies build their own board grammar in
	// MatrixReferenceCardView; no per-type body decoration here.
	Q_UNUSED(body);
}

void MatrixSkin::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const
{
		if (info.kind() == FilterCardKind::Spacer)
			return;

		painter.setRenderHint(QPainter::Antialiasing, false);

		const QRect content = rect.adjusted(MatrixMetrics::railInset, 1, -1, -1);
		if (content.width() <= 0 || content.height() <= 0)
			return;
		if (info.inlineRow)
		{
			paintRowChrome(painter, content, info, tokens);
			return;
		}
		const int headerHeight = qMin(tokens.rowHeight, content.height());
		const QRect headerBand(content.left(), content.top(), content.width(), headerHeight);

		// Header band fill (the header widget itself is transparent).
		painter.fillRect(headerBand, QColor(info.selected ? tokens.surfaceRaised : tokens.cardHover));

		// Every card body is one dark board panel (invariant rule 3). The
		// body editors assemble that panel from several widgets standing on
		// the window ground, but the layout margins and spacing between them
		// let the lighter card face peek through, which read as flakes
		// floating mid-card (r3 judging on the VST body; the same speckle
		// pattern was measured on the other card types). Flooding the whole
		// body band with the ground the editors already paint fuses their
		// panels, strips and cells into one block.
		if (content.height() > headerHeight)
			painter.fillRect(QRect(content.left(), content.top() + headerHeight,
				content.width(), content.height() - headerHeight), QColor(tokens.background));

		// A remark row (pure comment) is addressable but carries no signal
		// state: full grid ink and hover pre-light like an enabled row, but
		// no status lamp.
		const bool remark = info.kind() == FilterCardKind::Comment;

		// Faint column grid, clipped to the header band: the row body stays a
		// calm opaque panel regardless of editor widget opacity (invariant
		// rule 3 of the constitution).
		QColor gridColor(tokens.border);
		const int gridAlpha = tokens.dark ? 80 : 90;
		gridColor.setAlpha((info.enabled || remark) ? gridAlpha : gridAlpha / 2);
		painter.setPen(QPen(gridColor, 1));
		for (int x = content.left() + MatrixMetrics::gridPitch; x < content.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, headerBand.top(), x, headerBand.bottom());

		// 1px rule between the header cell and the body cell.
		if (content.height() > headerHeight)
		{
			painter.setPen(QPen(QColor(tokens.border), 1));
			painter.drawLine(content.left(), content.top() + headerHeight, content.right(), content.top() + headerHeight);
		}

		// Crosspoint hover: the row band and the coordinate-column band light
		// up; their intersection is the crosspoint.
		if (info.hovered && (info.enabled || remark))
		{
			QColor rowBand(tokens.accent);
			rowBand.setAlpha(22);
			painter.fillRect(headerBand, rowBand);
			const QRect columnBand(content.left(), content.top(),
				qMin(MatrixMetrics::coordinateBandWidth, content.width()), content.height());
			QColor columnColor(tokens.accent);
			columnColor.setAlpha(14);
			painter.fillRect(columnBand, columnColor);
			QColor crosspoint(tokens.accent);
			crosspoint.setAlpha(26);
			painter.fillRect(QRect(columnBand.left(), headerBand.top(), columnBand.width(), headerBand.height()), crosspoint);
		}

		// Status lamp in the left gutter: solid green = active, hollow amber
		// = bypassed; a remark gets no lamp. Gate rows post the engine's
		// branch decision in the same lamp grammar, and a swallowed line
		// un-lights its lamp. The analysis facts are advisory and only read
		// here, at paint time.
		if (!remark)
			paintStatusLamp(painter, QRect(content.left() + 1, content.top() + headerHeight / 2 - 3, 5, 5), info, tokens);

		paintEvalReadout(painter, headerBand, info, tokens);
	}

static void paintEvalReadout(QPainter& painter, const QRect& headerBand, const CommandRowInfo& info, const SkinTokens& tokens)
	{
		// Computed-value readout: the engine's Eval result or the substituted
		// inline-expression text in a boxed sunken mono cell, right-aligned
		// in the header. Body ink; a parser fault posts in danger. Paint-time
		// only by design: the facts go stale between an edit and the next
		// analysis run, and this cell repaints with them instead of baking
		// them into a construction-time label.
		if (!info.evalText.isEmpty() || (info.kind() == FilterCardKind::Eval && info.valueError))
		{
			const QString reading = QStringLiteral("= ")
				+ (info.evalText.isEmpty() ? QStringLiteral("ERR") : info.evalText);
			QFont mono(tokens.monoFontFamily);
			mono.setPointSizeF(8.5);
			mono.setBold(true);
			const QFontMetrics metrics(mono);
			// The button train lives in the fixed column after the coordinate
			// band now, so the band's right end is clear ground; only a
			// small margin is kept. The cell never grows left into the
			// coordinate band, and a reading that still does not fit is
			// elided, never squeezed.
			const int reserved = 12;
			const int cellRight = headerBand.right() - reserved;
			const int maxWidth = qMin(220, cellRight - headerBand.left() - MatrixMetrics::coordinateBandWidth);
			if (maxWidth > 40)
			{
				const QString elided = metrics.elidedText(reading, Qt::ElideRight, maxWidth - 12);
				const int cellWidth = qMin(maxWidth, metrics.horizontalAdvance(elided) + 12);
				const QRect cellRect(cellRight - cellWidth,
					headerBand.top() + (headerBand.height() - MatrixMetrics::knobCellHeight) / 2,
					cellWidth, MatrixMetrics::knobCellHeight);
				// A sunken fill is the cell; the card already draws the
				// one closed box around it.
				painter.fillRect(cellRect, QColor(tokens.surfaceSunken));
				painter.setFont(mono);
				painter.setPen(info.valueError ? QColor(tokens.danger) : QColor(tokens.text));
				painter.drawText(cellRect, Qt::AlignCenter, elided);
			}
		}
	}

bool MatrixSkin::paintChannelBadge(QPainter& painter, const QRect& rect, const QString& channel,
	bool virtualChannel, const SkinTokens& tokens) const
{
	// A channel scope is an address printed on the header band, in the
	// coordinate ink: bold mono, muted, no cell. The shared chip boxed it
	// inside the card's own box and dressed it in the routing palette, a
	// colour the board reserves for state (rule 1). A virtual channel keeps
	// the dash, as a rule under the name.
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	QFont font(tokens.monoFontFamily);
	font.setPixelSize(12);
	font.setBold(true);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
	painter.setFont(font);
	const QColor ink(tokens.mutedText);
	painter.setPen(ink);
	painter.drawText(rect, Qt::AlignCenter, channel);
	if (virtualChannel)
	{
		const QFontMetrics metrics(font);
		const int width = metrics.horizontalAdvance(channel);
		const int left = rect.center().x() - width / 2;
		const int y = rect.center().y() + metrics.ascent() / 2 + 2;
		painter.setPen(QPen(ink, 1, Qt::DashLine));
		painter.drawLine(left, y, left + width - 1, y);
	}
	return true;
}

QIcon MatrixSkin::powerToggleIcon(bool enabled, const SkinTokens& tokens) const
{
	// The power cell is a processor's ON/OFF key, two grid pitches wide
	// (#456): every other header action on this board is a bare glyph, so a
	// power glyph among them read as one more action. ON is mono body ink on
	// the sunken fill the sheet gives the checked cell; OFF is muted ink over
	// a dotted rule, the cancellation the board already prints, with no fill.
	// ON/OFF rather than IN/BYP: IN asks for OUT (maintainer, 2026-11-09).
	// The legend is painted into the icon so it follows the state change the
	// row already handles (FilterCardRow sets the icon, not the text).
	QIcon icon;
	const QColor ink(enabled ? tokens.text : tokens.mutedText);
	const QString legend = enabled ? QStringLiteral("ON") : QStringLiteral("OFF");
	QFont font(tokens.monoFontFamily);
	font.setPixelSize(11);
	font.setBold(true);
	for (const qreal dpr : { 1.0, 2.0 })
	{
		QPixmap pixmap(QSize(MatrixMetrics::powerKeyWidth, 16) * dpr);
		pixmap.setDevicePixelRatio(dpr);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setFont(font);
		painter.setPen(ink);
		painter.drawText(QRect(0, 0, MatrixMetrics::powerKeyWidth, 14), Qt::AlignCenter, legend);
		if (!enabled)
		{
			const int width = QFontMetrics(font).horizontalAdvance(legend);
			const int left = (MatrixMetrics::powerKeyWidth - width) / 2;
			QPen rule(ink, 1);
			rule.setDashPattern({ 2.0, 2.0 });
			painter.setPen(rule);
			painter.drawLine(left, 14, left + width - 1, 14);
		}
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}

bool MatrixSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const bool headRow = info.scopeRole == CommandRowInfo::ScopeRole::Head;
	const bool tailRow = info.scopeRole == CommandRowInfo::ScopeRole::Tail;
	const int logic = info.logicDepth;
	if (info.scopeRole == CommandRowInfo::ScopeRole::None)
		return false;

	painter.setRenderHint(QPainter::Antialiasing, false);

	// Lane and scope geometry from the row widget: the indent unit, how many
	// bands are drawn, where the card face starts, where a band's centre is
	// and how many bands are channel groups. The branch/tail rows' extra unit
	// (logicSiblingsIndentAsMembers) is already folded into laneCount,
	// because the same call sets the row's own margin.
	const int h = size.height();
	const int cardLeft = info.cardLeft;
	// Rules sit on the centres of the existing indent bands; the bracket
	// claims no positions of its own.
	const auto bandCenter = [&info](int level) { return info.laneCenter(level); };

	// The innermost logicDepth bands are If lanes; any bands outside them
	// are channel groups and keep a quiet 1px border-ink rule, one ink
	// rank below the bracket, so scope reads above grouping.
	const int channelLevels = info.channelLaneCount;
	painter.setPen(QPen(QColor(tokens.border), 1));
	for (int level = 0; level < channelLevels; level++)
		painter.drawLine(bandCenter(level), 0, bandCenter(level), h);

	painter.setPen(QPen(QColor(tokens.mutedText), 1));
	const int junctionY = static_cast<int>(info.headerCenterY);
	if (headRow)
	{
		for (int level = channelLevels; level < channelLevels + logic; level++)
			painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
		// The bracket opens under the head: its rule first shows in the
		// margin below the gate's full-width cell.
		const int own = bandCenter(channelLevels + logic);
		painter.drawLine(own, h - 4, own, h);
	}
	else if (tailRow)
	{
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
		// The closing L-corner: down to the tail's centre line, then a
		// half-pitch tick to the EndIf cell's edge.
		const int own = bandCenter(channelLevels + logic - 1);
		painter.drawLine(own, 0, own, junctionY);
		painter.drawLine(own, junctionY, cardLeft - 1, junctionY);
	}
	else
	{
		// Members and branch rows: every open bracket passes straight
		// through. ElseIf/Else post their state on their own cells (the
		// gate lamp), not on the bracket.
		for (int level = channelLevels; level < channelLevels + logic; level++)
			painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
	}
	return true;
}
