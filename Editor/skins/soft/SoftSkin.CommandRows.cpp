/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QCoreApplication>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
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
// the summary as written - an honest fallback, the dashed raw well in the
// body already holds the source.
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

// One calm silhouette for every command type: a 12px rounded card one
// value step above the window, "shadowed" only by that step and a very
// light 1px border. Hover lifts the whole card one more value step
// (QSS :hover re-evaluates at paint time, so the inline rule is enough).
// A commented-out row sinks flush into the window background and keeps
// only a dashed outline - an empty slot, not an alarm.
QString SoftSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& t) const
{
	if (!info.enabled)
	{
		return QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px dashed %2; border-radius: %3px; }")
			.arg(t.background, t.border)
			.arg(t.borderRadius);
	}

	const QString borderColor = info.focused ? t.focusRing : (info.selected ? t.accent : t.border);
	const QString backgroundColor = info.selected ? t.cardSelected : t.card;
	const QString hoverColor = info.selected ? t.cardSelected : t.cardHover;
	return QStringLiteral(
		"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
		"QFrame#FilterCardRow:hover { background: %4; }")
		.arg(backgroundColor, borderColor)
		.arg(t.borderRadius)
		.arg(hoverColor);
}

// No header strip: the header shares the card surface so the row reads as
// one roomy rounded object (macOS System Settings rows have no banded
// title bar). Hierarchy inside the header comes from type tile, title size
// and whitespace, all handled in the QSS sheets.
QString SoftSkin::cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const
{
	return QStringLiteral("QWidget#FilterCardHeader { background: transparent; }");
}

// The row's type badge wears the picker's pastel grammar instead of the
// shared saturated pill. The ink is a deep warm neutral on the pastel
// chip - white text on a pastel is exactly the kind of low-contrast
// anxiety this skin removes. A sleeping (commented-out) row sinks its
// chip toward the window background.
BadgeTreatment SoftSkin::badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
	const QString& badgeToken, const SkinTokens& t) const
{
	Q_UNUSED(badgeToken);
	const bool dark = skinIsDark(t);
	const QColor pastel = softPastelize(QColor(typeColor), dark);
	if (!info.enabled)
	{
		const QColor sleeping = mixColor(pastel, QColor(t.background), 0.62);
		return {
			QStringLiteral("color:%1; border-color:transparent; background-color:%2;")
				.arg(t.mutedText, sleeping.name()),
			QColor(t.mutedText)
		};
	}
	return {
		QStringLiteral("color:#2B251D; border-color:transparent; background-color:%1;")
			.arg(pastel.name()),
		QColor(QStringLiteral("#2B251D"))
	};
}

// The plain-text rows (bare note lines and programmatic commands such
// as If/EndIf/Eval). FilterCardRow lays these styles inline, so QSS
// cannot reach them; construction time is the hook's moment.
void SoftSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
	const SkinTokens& tokens) const
{
	Q_UNUSED(card);
	if (info.legacyRow
		|| !FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine))
		return;

	// Sentence conditions: a simple If/Eval line is retold in the header
	// as a friendly body-typeface sentence ("If device is Speakers",
	// "Otherwise", "Set x to 5").
	// The rewrite is queued because the row constructor
	// calls rebuildSummary() right after this hook, which would restore
	// the as-written summary immediately; the queued call lands once the
	// row has settled (the gallery's processEvents() delivers it too).
	// Known limit: a later rebuildSummary() (raw edit, row reshuffle)
	// restores the as-written text until the row is rebuilt.
	if (header != nullptr && info.type != QStringLiteral("text") && !info.dynamicLine)
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

	const SkinTokens& t = tokens;
	if (QLabel* glyph = body->findChild<QLabel*>(QStringLiteral("FilterCardRawGlyph")))
		glyph->setVisible(false);
	if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
	{
		raw->setStyleSheet(QStringLiteral(
			"QLabel#FilterCardRawText { background:%1; color:%2; border:1px dashed %3; border-radius:16px; padding:8px 14px; font-family:\"%4\"; }"
			"QLabel#FilterCardRawText:disabled { background:%5; color:%6; }")
			.arg(t.surfaceSunken, t.text, t.border, t.monoFontFamily, t.background, t.mutedText));
	}
}

// The If block is held in a pastel arm: one quiet rounded bar per scope
// level in the gutter, born under the If sentence and closed with its
// cap on the EndIf row. Level math: for members the if-lanes are the
// innermost logicDepth bands after the depth-logicDepth channel bands.
bool SoftSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const bool ifFamily = info.type == QStringLiteral("if");
	const bool headRow = ifFamily && info.command == QStringLiteral("if");
	const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
	const int logic = info.logicDepth;
	if (!headRow && logic <= 0)
		return false;

	const QColor card(tokens.card);
	const QColor accent(tokens.accent);
	// Live scope: the value-arc pastel. Sleeping stretch: the bipolar
	// track's far mix - present and quiet, one step off the ground.
	const QColor arm = mixColor(accent, card, 0.25);
	const QColor armResting = mixColor(accent, card, 0.78);

	// Lane geometry from the row widget; see CommandRowInfo. The extra indent
	// unit branch/tail rows mount with is already in laneCount, so the arm
	// passes them instead of dying behind their full-width faces.
	const int unit = info.laneUnit;
	const int h = size.height();
	const int indentUnits = info.laneCount;
	const int channelLevels = qMax(0, indentUnits - logic);
	const bool resting = !info.enabled || info.lineSkipped;

	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);

	// An enclosing Channel group keeps its constitutional SoftShadow
	// band. The hook sees no per-row type colour (the shared rail tints
	// by it), so the shade leans on the border token - quieter, same
	// silhouette.
	if (channelLevels > 0)
	{
		// The band itself, not its centre: laneCenter gives the middle, so
		// step back half a unit for the left edge.
		const QRectF band(info.laneCenter(channelLevels - 1) - unit / 2.0, 0, unit, h);
		QLinearGradient shade(band.left(), 0, band.right(), 0);
		shade.setColorAt(0, withAlpha(QColor(tokens.border), 110));
		shade.setColorAt(1, withAlpha(QColor(tokens.border), 0));
		painter.fillRect(band, shade);
	}

	// One bar per scope, centred in its indent band. Straight runs
	// overshoot the row's clip so neighbouring rows tile into one
	// continuous arm; only the arm's first and last row show a cap.
	const auto laneX = [&info](int level) {
		// Two pixels left of the band centre, so the 4px bar straddles it.
		return double(info.laneCenter(level)) - 2.0;
	};
	const auto runBar = [&](int level, const QColor& color) {
		painter.setBrush(color);
		painter.drawRoundedRect(QRectF(laneX(level), -4.0, 4.0, h + 8.0), 2.0, 2.0);
	};

	// Outer scopes hold straight through every row of an inner block.
	const int ownLevel = channelLevels + logic - (headRow ? 0 : 1);
	for (int level = channelLevels; level < ownLevel; level++)
		runBar(level, arm);

	if (headRow)
	{
		// The arm begins BELOW the sentence: a rounded fingertip peeking
		// out of the row's bottom margin, which the first member row
		// carries on. A false condition relaxes the fingertip along with
		// the sleeping members it announces (branchState is fresh at
		// paint time by contract).
		painter.setBrush(resting || info.branchState == 0 ? armResting : arm);
		painter.drawRoundedRect(QRectF(laneX(ownLevel), h - 4.0, 4.0, 8.0), 2.0, 2.0);
	}
	else if (tailRow)
	{
		// The arm ends here: the bar falls to the row's centre line and
		// closes with its stadium cap.
		const qreal capY = 4.0 + tokens.rowHeight / 2.0;
		painter.setBrush(resting ? armResting : arm);
		painter.drawRoundedRect(QRectF(laneX(ownLevel), -4.0, 4.0, capY + 4.0), 2.0, 2.0);
	}
	else
	{
		// Members and branch rows: the arm passes at full height; a
		// swallowed line relaxes only its own stretch.
		runBar(ownLevel, resting ? armResting : arm);
	}
	return true;
}

// ElseIf/Else/EndIf mount one indent unit past their head, with the
// members, so the pastel arm passes them instead of dying behind their
// full-width faces.
bool SoftSkin::logicSiblingsIndentAsMembers() const
{
	return true;
}
