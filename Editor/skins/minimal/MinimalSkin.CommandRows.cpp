/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MinimalSkin.h"

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSpacerItem>
#include <QWidget>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/minimal/MinimalChannelInk.h"
#include "Editor/skins/minimal/cards/MinimalReferenceCardView.h"
#include "Editor/widgets/FilterCardModel.h"

namespace
{
// Leading type glyph for the line head, plain ASCII for the mapped types so
// every mono fallback font covers it. The glyph is shape information ("which
// kind of line is this"); the colour tag next to it stays the badge token.
QString minimalTypeGlyph(const QString& type)
{
	const FilterCardKind kind = filterCardKindFromType(type);
	if (kind == FilterCardKind::Biquad)
		return QStringLiteral("~");
	if (kind == FilterCardKind::Include)
		return QStringLiteral(">>");
	if (kind == FilterCardKind::Vst)
		return QStringLiteral("[]");
	if (kind == FilterCardKind::Copy)
		return QStringLiteral("->");
	if (kind == FilterCardKind::NewChannel)
		return QStringLiteral("+");
	if (kind == FilterCardKind::Send)
		return QStringLiteral("=>");
	if (kind == FilterCardKind::Comment)
		return QStringLiteral("#");
	if (kind == FilterCardKind::Spacer)
		return QString();
	// Unmapped commands keep the fixed-width column so line heads stay
	// aligned; the middle dot (U+00B7) deliberately carries no further
	// meaning. Built from a code point so the source stays pure ASCII (no
	// /utf-8 flag is set for MSVC).
	return QString(QChar(0x00B7));
}
}

QString MinimalSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	// One flat line per command: 1px hairline box, square corners, and
	// state expressed as background-value steps only. Disabled rows fall
	// one step below the resting card; a line a false If branch swallowed
	// takes the same step down (to the engine both are dead code). The
	// '#' glyph and the readout column keep skipped and commented apart.
	//
	// Cursor and marks are achromatic (maintainer, 2026-08-24: the blue
	// cardSelected step and the accent frame broke "accent only while
	// active"): the focused row - the list cursor - and every marked row
	// take a body-ink hairline, nothing is filled, and the cursor's row
	// number goes reverse video in cardHeaderStyle below. Accent ink now
	// appears in the list only while a control is actually being worked.
	const bool sunken = !info.enabled || info.lineSkipped;
	const QString background = sunken ? tokens.surface : tokens.card;
	const QString borderColor = info.focused || info.selected ? tokens.text : tokens.border;
	QString style = QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 0; }")
		.arg(background, borderColor);
	if (!info.selected)
	{
		style += QStringLiteral(" QFrame#FilterCardRow:hover { background: %1; }")
			.arg(sunken ? tokens.card : tokens.cardHover);
	}
	return style;
}
QString MinimalSkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	// No separate header plate: the row reads as a single text line, so
	// the header inherits the frame's background through transparency.
	QString style = QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0; }");
	// The list cursor is the terminal's reverse-video block, printed on the
	// row number - the bluntest marker a text instrument has (the same
	// canon the picker and the Device chips already follow).
	if (info.focused)
	{
		style += QStringLiteral(" QLabel#FilterCardNumber { background: %1; color: %2; }")
			.arg(tokens.text, tokens.background);
	}
	return style;
}

bool MinimalSkin::paintChannelBadge(QPainter& painter, const QRect& rect, const QString& channel,
	bool virtualChannel, const SkinTokens& tokens) const
{
	// Terminal print, not a GUI chip (maintainer, 2026-08-24): the header's
	// channel-scope tokens follow the Copy listing's grammar - the designed
	// console ink on bare ground, uppercase mono, no pill chrome. Virtual
	// channels keep the dashed frame (the unverified-token dash grammar).
	// SemiBold like the listing's tokens (MinimalChannelInk.h).
	const QColor ink = minimalChannelInk(channel, skinIsDark(tokens));
	QFont font(tokens.monoFontFamily);
	font.setPointSizeF(9.5);
	font.setWeight(QFont::DemiBold);
	painter.setFont(font);
	painter.setPen(ink);
	painter.drawText(rect, Qt::AlignCenter, channel);
	if (virtualChannel)
	{
		painter.setPen(QPen(ink, 1.0, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(rect).adjusted(1.5, 2.5, -1.5, -2.5));
	}
	return true;
}
bool MinimalSkin::supportsRowMode() const
{
	// Modern rows: every command is one printed line, the header's parts
	// in its left column and the editor beside them.
	return true;
}

void MinimalSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
	const SkinTokens& tokens) const
{
	// A Modern row is one printed line: the left column gives up the card
	// header's side margins (its width is what an indented Filter row needs
	// to keep its last field inside a 960 px window), and the editor runs to
	// a 4 px margin before the frame's right hairline. Cards keep their
	// layout; this runs only for the row widget's own call.
	if (info.inlineRow && card != nullptr && card->layout() != nullptr)
	{
		card->layout()->setContentsMargins(0, 0, 4, 0);
		if (QBoxLayout* column = header != nullptr ? qobject_cast<QBoxLayout*>(header->layout()) : nullptr)
		{
			column->setContentsMargins(2, 2, 0, 2);
			column->setSpacing(2);
		}
		if (body != nullptr)
		{
			// The reference line lays itself out for the row (a loaded VST
			// unit takes two lines). The editor is mounted in the body by
			// now, and its own construction-time call never sees inlineRow.
			for (MinimalReferenceCardView* view : body->findChildren<MinimalReferenceCardView*>())
				view->setInlineRow(true);
			// The Filter editor separates its type selector and its three
			// parameter columns with 8 px spacers. The drums already carry
			// their own clear margin around the rims, so on one line the
			// spacers shrink to a 2 px hair and the width goes to the last
			// column, which an indented line under a Channel scope needs.
			if (QWidget* biquad = body->findChild<QWidget*>(QStringLiteral("BiQuadFilterGUI")))
			{
				if (QLayout* grid = biquad->layout())
				{
					for (int i = 0; i < grid->count(); i++)
					{
						if (QSpacerItem* spacer = grid->itemAt(i)->spacerItem())
							spacer->changeSize(2, spacer->sizeHint().height(), QSizePolicy::Fixed, QSizePolicy::Minimum);
					}
					grid->invalidate();
				}
			}
			// The Send line's device name is a data column: across one row
			// it gives way first (shared article 4). The combo sizes itself to
			// the longest endpoint name, which pushed the delay check box
			// out of the row in the longer languages; in a row its floor is
			// 10 characters plus the field's padding and caret (28 px right,
			// 10 px left, the hairline), and the name elides inside it.
			if (QComboBox* target = body->findChild<QComboBox*>(QStringLiteral("SendTargetCombo")))
			{
				target->ensurePolished();
				target->setMinimumWidth(10 * target->fontMetrics().horizontalAdvance(QLatin1Char('X')) + 40);
			}
		}
	}
	// A raw line (a bare note, or a programmatic command like If/EndIf
	// the editor does not model) is printed bare on the body strip: no
	// box, no input ground. The shared raw card lays its chrome inline,
	// so QSS cannot reach it and the override happens here. Rows are
	// rebuilt on skin/theme switches, so construction-time token values
	// stay current.
	if (FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine) && body != nullptr)
	{
		const SkinTokens& tk = tokens;
		if (QLabel* rawText = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
		{
			rawText->setStyleSheet(QStringLiteral("QLabel#FilterCardRawText { background: transparent; color: %1; border: 0; border-radius: 0; padding: 2px 0; font-family: \"%2\"; }")
				.arg(info.enabled ? tk.text : tk.mutedText, tk.monoFontFamily));
		}
	}
	// Leading type glyph at the line head. Only modern card rows carry a
	// header here; the Include/VST body editors and the frozen legacy
	// rows consult the hook with header == nullptr and stay untouched.
	if (header == nullptr)
		return;
	QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
	if (headerLayout == nullptr)
		return;
	// A commented-out line leads with the comment marker it actually
	// carries in the config file; the marker is information, not decor.
	const QString glyph = info.enabled ? minimalTypeGlyph(info.type) : QStringLiteral("#");
	if (glyph.isEmpty())
		return;
	QLabel* glyphLabel = new QLabel(glyph, header);
	glyphLabel->setObjectName(QStringLiteral("MinimalTypeGlyph"));
	glyphLabel->setAlignment(Qt::AlignCenter);
	glyphLabel->setMinimumWidth(18);
	headerLayout->insertWidget(0, glyphLabel);
}
// The watch readout column: the analysis fact for a line is printed as
// a right-aligned DM Mono column in the header. Painted here rather
// than as a construction-time label because the facts refresh with
// every analysis run and only paint time is guaranteed to see the
// fresh values (prepareCommandRow would freeze the first, stale ones).
void MinimalSkin::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const bool ifFamily = info.kind() == FilterCardKind::If;
	const bool verdictRow = ifFamily && info.command != QStringLiteral("endif");
	const bool valueRow = !ifFamily && (!info.evalText.isEmpty() || info.valueError);
	if (!verdictRow && !valueRow)
		return;

	QString figure;
	QColor ink;
	bool bold = false;
	if (verdictRow)
	{
		if (info.branchState == 1)
		{
			figure = QStringLiteral("TRUE");
			ink = QColor(tokens.text);
		}
		else if (info.branchState == 0)
		{
			figure = QStringLiteral("FALSE");
			ink = QColor(tokens.mutedText);
		}
		else if (info.branchState == 3)
		{
			figure = QStringLiteral("ERR");
			ink = QColor(tokens.text);
			bold = true;
		}
		else
		{
			// Unknown (-1) and short-circuited (2) read the same: the
			// register holds no measurement. The em dash (U+2014, built
			// from a code point - pure-ASCII source) sinks one step
			// below the secondary ink; no third grey token exists and
			// none is created here, the step is mixed from tokens.
			figure = QString(QChar(0x2014));
			ink = mixColor(QColor(tokens.mutedText), QColor(tokens.card), 0.45);
		}
	}
	else if (info.valueError)
	{
		figure = QStringLiteral("ERR");
		ink = QColor(tokens.text);
		bold = true;
	}
	else
	{
		figure = QStringLiteral("= ") + info.evalText;
		ink = QColor(tokens.mutedText);
	}

	// Right-aligned in the header band, a hair short of the right edge.
	// The header button train sits in the fixed column at the left now
	// (after the row number), so the right end of the band is clear
	// ground. The If/Eval summaries are empty by model contract, so the
	// column prints on empty line space; a cramped card drops the
	// readout rather than colliding with the line head.
	// In a Modern row the title stands in the row's top band (or on the
	// whole row, for a row that centres its markers), not in a header strip
	// of rowHeight, so the column centres on the same line as the title.
	const int headerHeight = qMin(tokens.rowHeight, rect.height());
	const int headerTop = info.inlineRow
		? qRound(info.headerCenterY) - CommandRowInfo::cardMarginTop - headerHeight / 2
		: rect.top();
	const QRect column(rect.left() + 8, headerTop, rect.width() - 24, headerHeight);
	if (column.width() < 60)
		return;

	QFont font(tokens.monoFontFamily);
	font.setPointSizeF(10.0);
	font.setBold(bold);
	painter.setFont(font);
	painter.setPen(ink);
	painter.drawText(column, Qt::AlignRight | Qt::AlignVCenter,
		QFontMetrics(font).elidedText(figure, Qt::ElideRight, column.width()));
}
// The If-block scope in the gutter is a code editor's indent guide:
// one crisp 1px border-ink hairline per scope level, antialiasing off.
// The channel-group level is drawn by the same rule. Branch rows keep
// their semantic indentation (logicSiblingsIndentAsMembers answers
// false), so the innermost guide pauses on their line the way an
// editor's guides do.
bool MinimalSkin::logicSiblingsIndentAsMembers() const
{
	return false;
}

bool MinimalSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	// An unindented row has no gutter; leave the (empty) default path.
	if (info.depth <= 0)
		return false;
	painter.setRenderHint(QPainter::Antialiasing, false);
	// The guides go dashed where they pass a swallowed line: the dash
	// grammar ("no verified substance") extended to a stretch the engine
	// is not running (paired with the frame's background step in
	// cardFrameStyle).
	painter.setPen(QPen(QColor(tokens.border), 1, info.lineSkipped ? Qt::DotLine : Qt::SolidLine));
	for (int level = 0; level < info.depth; level++)
	{
		// Centred in its indent band. The centre comes from the row widget
		// now, which is also what sets the card face's own left margin.
		const int x = info.laneCenter(level);
		painter.drawLine(x, 0, x, size.height());
	}
	return true;
}
