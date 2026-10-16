/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CommandRowFacts.h"

#include <QtGlobal>

#include "engine/ConfigLoadTrace.h"
#include "Editor/widgets/FilterCardModel.h"

int CommandRowFacts::indentUnits(const FilterCardDescriptor& descriptor, bool logicSiblingsIndentAsMembers)
{
	// Member level is one unit past the branch/tail row's own semantic level
	// (depth already carries any enclosing channel group, so +1 keeps mixed
	// Channel x If nesting aligned with the block members).
	if (descriptor.type == QStringLiteral("if") && descriptor.badge != QStringLiteral("IF")
		&& logicSiblingsIndentAsMembers)
		return descriptor.depth + 1;
	return descriptor.depth;
}

CommandRowInfo::ScopeRole CommandRowFacts::scopeRole(const QString& type, const QString& command, int logicDepth)
{
	const bool ifFamily = type == QStringLiteral("if");
	if (ifFamily && command == QStringLiteral("if"))
		return CommandRowInfo::ScopeRole::Head;
	if (logicDepth <= 0)
		return CommandRowInfo::ScopeRole::None;
	if (ifFamily && (command == QStringLiteral("elseif") || command == QStringLiteral("else")))
		return CommandRowInfo::ScopeRole::Branch;
	if (ifFamily && command == QStringLiteral("endif"))
		return CommandRowInfo::ScopeRole::Tail;
	return CommandRowInfo::ScopeRole::Member;
}

void CommandRowFacts::foldLoadFacts(CommandRowInfo& info, const QList<ConfigLoadTraceEntry>& facts)
{
	// Advisory by contract: facts go stale between an edit and the next
	// analysis run.
	for (const ConfigLoadTraceEntry& fact : facts)
	{
		switch (fact.kind)
		{
		case ConfigLoadTraceEntry::Kind::Condition:
			info.branchState = fact.result == ConfigLoadTraceEntry::Result::True ? 1
				: fact.result == ConfigLoadTraceEntry::Result::False ? 0
				: fact.result == ConfigLoadTraceEntry::Result::Error ? 3 : 2;
			break;
		case ConfigLoadTraceEntry::Kind::ElseBranch:
			info.branchState = fact.active ? 1 : 0;
			break;
		case ConfigLoadTraceEntry::Kind::Eval:
			info.evalText = QString::fromStdWString(fact.text);
			info.valueError = info.valueError || fact.error;
			break;
		case ConfigLoadTraceEntry::Kind::InlineValue:
			// The engine preserves the space after the colon; trim at
			// this UI boundary so readouts do not start with a gap.
			if (info.evalText.isEmpty())
				info.evalText = QString::fromStdWString(fact.text).trimmed();
			info.valueError = info.valueError || fact.error;
			break;
		case ConfigLoadTraceEntry::Kind::SkippedLine:
			info.lineSkipped = true;
			break;
		case ConfigLoadTraceEntry::Kind::ParseError:
			// Several can arrive for one line if a factory reports more than
			// once; the first is the one that stopped it.
			if (info.parseError.isEmpty())
				info.parseError = QString::fromStdWString(fact.text);
			break;
		case ConfigLoadTraceEntry::Kind::SetupError:
			// The whole configuration rolled back. The exception text is a
			// developer's log line and stays in the log; the row only needs
			// to know that this line is the one that stopped the load.
			info.setupFailed = true;
			break;
		}
	}
}

CommandRowInfo CommandRowFacts::describe(const FilterCardDescriptor& descriptor, const RowContext& context,
	const QList<ConfigLoadTraceEntry>& facts)
{
	CommandRowInfo info;
	info.type = descriptor.type;
	info.command = descriptor.command.toLower();
	info.enabled = descriptor.enabled;
	info.selected = context.selected;
	info.focused = context.focused;
	info.depth = descriptor.depth;
	info.logicDepth = descriptor.logicDepth;
	info.dynamicLine = descriptor.dynamicLine;

	// The lane geometry: indentUnits is the same answer the row's own layout
	// margin uses, so a skin's gutter cannot disagree with the card face it
	// sits beside.
	info.laneUnit = context.laneUnit;
	info.laneCount = indentUnits(descriptor, context.logicSiblingsIndentAsMembers);
	info.cardLeft = CommandRowInfo::cardMarginLeft + info.laneCount * info.laneUnit;

	info.scopeRole = scopeRole(info.type, info.command, info.logicDepth);
	info.channelLaneCount = qMax(0, info.laneCount - info.logicDepth);
	info.headerCenterY = CommandRowInfo::cardMarginTop + context.rowHeight / 2.0;

	foldLoadFacts(info, facts);
	return info;
}
