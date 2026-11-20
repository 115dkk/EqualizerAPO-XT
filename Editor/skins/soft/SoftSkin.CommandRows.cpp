/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SoftSkin.h"

#include <QCoreApplication>
#include <QBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterStateGuard>
#include <QRegularExpression>
#include <QWidget>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/FilterCardModel.h"

namespace
{
// The friendly-sentence grammar of the dynamic commands. Only a right-hand
// side this simple may enter a sentence: a quoted string (the quotes come
// off), a number, or true/false. Everything else counts as complex and keeps
// the summary as written - an honest fallback, the source field in the body
// already holds the line.
QString softSentenceLiteral(const QString& raw)
{
	const QString value = raw.trimmed();
	if (value.size() >= 2)
	{
		const QChar quote = value.at(0);
		if ((quote == QLatin1Char('"') || quote == QLatin1Char('\'')) && value.endsWith(quote))
		{
			const QString inner = value.mid(1, value.size() - 2);
			if (!inner.contains(QLatin1Char('"')) && !inner.contains(QLatin1Char('\'')))
				return inner;
			return QString();
		}
	}
	static const QRegularExpression simple(QStringLiteral("^([-+]?[0-9]+(\\.[0-9]+)?|true|false)$"),
		QRegularExpression::CaseInsensitiveOption);
	return simple.match(value).hasMatch() ? value : QString();
}

// Retells a simple If/ElseIf comparison (identifier op literal), an Else /
// EndIf marker or a simple Eval assignment as the sentence a settings app
// would dare to show. Returns an empty string when the line is not that
// simple, so the caller leaves the as-written summary alone.
QString softFriendlySentence(const QString& command, const QString& asWritten)
{
	// Else/EndIf carry no expression (the engine ignores text after their
	// colon), so their sentence stands alone. When someone did write
	// something there, keeping it visible is the honest reading.
	if (command == QStringLiteral("else"))
		return asWritten.trimmed().isEmpty() ? QCoreApplication::translate("SoftSkin", "Otherwise") : QString();
	if (command == QStringLiteral("endif"))
		return asWritten.trimmed().isEmpty() ? QCoreApplication::translate("SoftSkin", "End of the rule") : QString();

	if (command == QStringLiteral("eval"))
	{
		// A simple assignment ("x = 5") becomes "Set x to 5"; a computed
		// expression stays as written.
		static const QRegularExpression assignment(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\s*=(?!=)\\s*(.+)$"));
		const QRegularExpressionMatch match = assignment.match(asWritten.trimmed());
		const QString value = match.hasMatch() ? softSentenceLiteral(match.captured(2)) : QString();
		if (value.isEmpty())
			return QString();
		return QCoreApplication::translate("SoftSkin", "Set %1 to %2").arg(match.captured(1), value);
	}

	if (command != QStringLiteral("if") && command != QStringLiteral("elseif"))
		return QString();

	static const QRegularExpression comparison(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\s*(==|!=|<=|>=|<|>)\\s*(.+)$"));
	const QRegularExpressionMatch match = comparison.match(asWritten.trimmed());
	const QString value = match.hasMatch() ? softSentenceLiteral(match.captured(3)) : QString();
	if (value.isEmpty())
		return QString();

	// One complete sentence per operator, not an operator phrase slotted
	// into a shared "If %1 is %2" frame: particles and word order change
	// with the comparison in languages like Korean, so only the whole
	// sentence can be translated.
	const QString op = match.captured(2);
	QString sentence;
	if (command == QStringLiteral("if"))
	{
		if (op == QStringLiteral("=="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is %2");
		else if (op == QStringLiteral("!="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is not %2");
		else if (op == QStringLiteral(">="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is at least %2");
		else if (op == QStringLiteral(">"))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is more than %2");
		else if (op == QStringLiteral("<="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is at most %2");
		else
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is less than %2");
	}
	else
	{
		if (op == QStringLiteral("=="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is %2");
		else if (op == QStringLiteral("!="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is not %2");
		else if (op == QStringLiteral(">="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is at least %2");
		else if (op == QStringLiteral(">"))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is more than %2");
		else if (op == QStringLiteral("<="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is at most %2");
		else
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is less than %2");
	}
	return sentence.arg(match.captured(1), value);
}
}

// Every command type is the same card: the card surface, a 1px line, the
// skin's surface radius. Hover takes the line one step heavier; the chosen
// card's line is the emphasis colour. Nothing tints the card - the accent
// never colours an area. A switched-off row keeps its frame; its switch is
// off and its contents fade (the sheets' :disabled rules).
QString SoftSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& t) const
{
	const bool marked = info.enabled && (info.selected || info.focused);
	const QString borderColor = marked ? (info.focused ? t.focusRing : t.accent) : t.border;
	return QStringLiteral(
		"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
		"QFrame#FilterCardRow:hover { border-color: %4; }")
		.arg(t.card, borderColor)
		.arg(t.borderRadius)
		.arg(marked ? borderColor : t.borderStrong);
}

// The header shares the card surface. What is in it is told apart by the
// type glyph, the title's weight and the space between.
QString SoftSkin::cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const
{
	return QStringLiteral("QWidget#FilterCardHeader { background: transparent; }");
}

// The type mark is the pictogram alone, in the body ink: no tile, no colour
// per type. The title beside it already names the command, and a colour per
// type would be a second, unlabelled code to learn.
BadgeTreatment SoftSkin::badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
	const QString& badgeToken, const SkinTokens& t) const
{
	Q_UNUSED(typeColor);
	Q_UNUSED(badgeToken);
	const QString ink = info.enabled ? t.text : t.mutedText;
	return {
		QStringLiteral("color:%1; border-color:transparent; background-color:transparent;").arg(ink),
		QColor(ink)
	};
}

// The plain-text rows (lines that are no command, and programmatic commands
// such as If/EndIf/Eval). FilterCardRow lays these styles inline, so QSS
// cannot reach them; construction time is the hook's moment.
void SoftSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
	const SkinTokens& tokens) const
{
	Q_UNUSED(card);
	if (info.legacyRow
		|| !FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine))
		return;

	// Sentence conditions: a simple If/Eval line is retold in the header
	// as a body-typeface sentence ("If device is Speakers", "Otherwise",
	// "Set x to 5").
	// The rewrite is queued because the row constructor
	// calls rebuildSummary() right after this hook, which would restore
	// the as-written summary immediately; the queued call lands once the
	// row has settled (the gallery's processEvents() delivers it too).
	// Known limit: a later rebuildSummary() (raw edit, row reshuffle)
	// restores the as-written text until the row is rebuilt.
	if (header != nullptr && info.kind() != FilterCardKind::Text && !info.dynamicLine)
	{
		if (QLabel* summary = header->findChild<QLabel*>(QStringLiteral("FilterCardSummary")))
		{
			const QString command = info.command;
			QMetaObject::invokeMethod(summary, [summary, command]() {
				const QString asWritten = summary->text();
				const QString sentence = softFriendlySentence(command, asWritten);
				if (sentence.isEmpty() || sentence == asWritten)
					return;
				// The as-written line stays reachable before the body is
				// ever expanded: the sentence's tooltip answers with it.
				if (!asWritten.trimmed().isEmpty())
					summary->setToolTip(asWritten);
				summary->setText(sentence);
			}, Qt::QueuedConnection);
		}
	}

	if (body == nullptr)
		return;

	// Source text sits in a field, in the monospace face: the one place the
	// skin shows a line as written. The terminal prompt glyph is dropped; the
	// field already says "this is text".
	const SkinTokens& t = tokens;
	if (QLabel* glyph = body->findChild<QLabel*>(QStringLiteral("FilterCardRawGlyph")))
		glyph->setVisible(false);
	QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText"));
	if (raw == nullptr)
		return;
	raw->setStyleSheet(QStringLiteral(
		"QLabel#FilterCardRawText { background:%1; color:%2; border:1px solid %3; border-radius:6px; padding:6px 10px;"
		" font-family:\"%4\"; font-size:14px; }"
		"QLabel#FilterCardRawText:disabled { color:%5; border-color:%6; }")
		.arg(t.card, t.text, t.controlEdge, t.monoFontFamily, t.mutedText, t.borderStrong));

	// A line that is no command changes nothing in the sound, and the row
	// says so beside the line, once, in the body ink.
	if (info.kind() != FilterCardKind::Text)
		return;
	QWidget* rawHost = raw->parentWidget();
	QBoxLayout* rawLayout = rawHost != nullptr ? qobject_cast<QBoxLayout*>(rawHost->layout()) : nullptr;
	if (rawLayout == nullptr || rawHost->findChild<QLabel*>(QStringLiteral("SoftNotAppliedNote")) != nullptr)
		return;
	rawLayout->setStretchFactor(raw, 0);
	QLabel* note = new QLabel(QCoreApplication::translate("SoftSkin", "Not applied to the audio."), rawHost);
	note->setObjectName(QStringLiteral("SoftNotAppliedNote"));
	note->setStyleSheet(QStringLiteral("QLabel#SoftNotAppliedNote { color:%1; font-weight:700; background:transparent; }")
		.arg(t.text));
	rawLayout->addWidget(note, 0, Qt::AlignVCenter);
	rawLayout->addStretch(1);
}

// A block is marked by one thin line down its left side, the way an editor
// marks an indented block: it starts under the If row, passes the member
// rows, and turns in at the middle of the EndIf row. Along a stretch the
// engine skips (a false branch, a switched-off line) the line is one step
// lighter. No colour: whether a branch is true is not the line's to say.
bool SoftSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	if (info.scopeRole == CommandRowInfo::ScopeRole::None)
	{
		// A row that only sits under a Channel command: the same line, at the
		// place the shared rail keeps for each channel level
		// (FilterCardRow::paintEvent).
		if (info.depth <= 0)
			return false;
		for (int level = 0; level < info.depth; level++)
			painter.fillRect(QRectF(8.0 + level * tokens.channelGroupIndent + 6.0, 0.0, 2.0, size.height()),
				QColor(tokens.borderStrong));
		return true;
	}
	const bool headRow = info.scopeRole == CommandRowInfo::ScopeRole::Head;
	const bool tailRow = info.scopeRole == CommandRowInfo::ScopeRole::Tail;
	const int logic = info.logicDepth;

	const QColor line(tokens.borderStrong);
	const QColor lineResting(tokens.border);
	const int h = size.height();
	const int channelLevels = info.channelLaneCount;
	const bool resting = !info.enabled || info.lineSkipped;

	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, false);

	// Lane geometry from the row widget; see CommandRowInfo. The line is 2px,
	// one pixel either side of the lane centre.
	const auto laneX = [&info](int level) {
			return double(info.laneCenter(level)) - 1.0;
		};
	const auto run = [&](int level, const QColor& color, qreal top, qreal bottom) {
			painter.fillRect(QRectF(laneX(level), top, 2.0, bottom - top), color);
		};

	// An enclosing Channel group is the same line: the rows a Channel command
	// applies to are a block too.
	for (int level = 0; level < channelLevels; level++)
		run(level, line, 0.0, h);

	// Outer scopes hold straight through every row of an inner block.
	const int ownLevel = channelLevels + logic - (headRow ? 0 : 1);
	for (int level = channelLevels; level < ownLevel; level++)
		run(level, line, 0.0, h);

	if (headRow)
	{
		// The line begins under the If row: the last pixels of this row's
		// bottom margin, which the first member row carries on.
		run(ownLevel, resting || info.branchState == 0 ? lineResting : line, h - 4.0, h);
	}
	else if (tailRow)
	{
		// The line ends here: down to the row's centre line, then a short
		// turn toward the card.
		const qreal capY = qRound(info.headerCenterY);
		const QColor color = resting ? lineResting : line;
		run(ownLevel, color, 0.0, capY + 1.0);
		painter.fillRect(QRectF(laneX(ownLevel), capY - 1.0, qMax(4.0, info.laneUnit / 2.0), 2.0), color);
	}
	else
		run(ownLevel, resting ? lineResting : line, 0.0, h);
	return true;
}
