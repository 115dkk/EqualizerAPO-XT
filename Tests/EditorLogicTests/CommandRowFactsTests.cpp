/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	CommandRowFacts: the row info a card row hands its skin (load facts
	folded into branch state, values and errors; lane and If-scope
	geometry), derived without the widget (audit #422 D4, S2).
*/

#include <QList>
#include <QString>

#include "engine/ConfigLoadTrace.h"
#include "Editor/widgets/CommandRowFacts.h"
#include "Editor/widgets/FilterCardModel.h"

#include "EditorLogicTestSupport.h"

namespace
{
ConfigLoadTraceEntry fact(ConfigLoadTraceEntry::Kind kind)
{
	ConfigLoadTraceEntry entry;
	entry.kind = kind;
	return entry;
}

ConfigLoadTraceEntry condition(ConfigLoadTraceEntry::Result result)
{
	ConfigLoadTraceEntry entry = fact(ConfigLoadTraceEntry::Kind::Condition);
	entry.result = result;
	return entry;
}

ConfigLoadTraceEntry textFact(ConfigLoadTraceEntry::Kind kind, const wchar_t* text, bool error = false)
{
	ConfigLoadTraceEntry entry = fact(kind);
	entry.text = text;
	entry.error = error;
	return entry;
}

int branchStateFor(const QList<ConfigLoadTraceEntry>& facts)
{
	CommandRowInfo info;
	CommandRowFacts::foldLoadFacts(info, facts);
	return info.branchState;
}

FilterCardDescriptor scoped(const QString& line, int depth, int logicDepth)
{
	FilterCardRowScope scope;
	scope.indent = depth;
	scope.logic = logicDepth;
	return FilterCardModel::describeRow(line, scope);
}
}

void testCommandRowFacts()
{
	// Branch truth: True, False, Error and any other result (not evaluated)
	// map to 1, 0, 3 and 2; no fact leaves it unknown.
	expectEqual(branchStateFor({}), -1, "no load fact leaves the branch state unknown");
	expectEqual(branchStateFor({ condition(ConfigLoadTraceEntry::Result::True) }), 1, "a true condition takes the branch");
	expectEqual(branchStateFor({ condition(ConfigLoadTraceEntry::Result::False) }), 0, "a false condition is a dead branch");
	expectEqual(branchStateFor({ condition(ConfigLoadTraceEntry::Result::Error) }), 3, "a condition error is an evaluation error");
	expectEqual(branchStateFor({ condition(ConfigLoadTraceEntry::Result::NotEvaluated) }), 2,
		"a condition the chain never reached is not evaluated");
	ConfigLoadTraceEntry activeElse = fact(ConfigLoadTraceEntry::Kind::ElseBranch);
	activeElse.active = true;
	expectEqual(branchStateFor({ activeElse }), 1, "an active Else takes the branch");
	activeElse.active = false;
	expectEqual(branchStateFor({ activeElse }), 0, "an inactive Else is a dead branch");

	// Eval beats an inline substitution whatever the order; an inline value
	// is trimmed; either one's error marks the value.
	{
		CommandRowInfo info;
		CommandRowFacts::foldLoadFacts(info, { textFact(ConfigLoadTraceEntry::Kind::InlineValue, L" 3 dB"),
			textFact(ConfigLoadTraceEntry::Kind::Eval, L"x = 3") });
		expectEqual(info.evalText, "x = 3", "an Eval result after an inline value wins");
	}
	{
		CommandRowInfo info;
		CommandRowFacts::foldLoadFacts(info, { textFact(ConfigLoadTraceEntry::Kind::Eval, L"x = 3"),
			textFact(ConfigLoadTraceEntry::Kind::InlineValue, L" 3 dB", true) });
		expectEqual(info.evalText, "x = 3", "an Eval result before an inline value wins");
		expectTrue(info.valueError, "an inline value error marks the value");
	}
	{
		CommandRowInfo info;
		CommandRowFacts::foldLoadFacts(info, { textFact(ConfigLoadTraceEntry::Kind::InlineValue, L"  -3 dB ") });
		expectEqual(info.evalText, "-3 dB", "an inline value is trimmed at the UI boundary");
		expectFalse(info.valueError, "a clean inline value is no error");
	}

	// The first parse error is the one that stopped the line; skipped lines
	// and setup failures are flags.
	{
		CommandRowInfo info;
		CommandRowFacts::foldLoadFacts(info, { textFact(ConfigLoadTraceEntry::Kind::ParseError, L"first"),
			textFact(ConfigLoadTraceEntry::Kind::ParseError, L"second"), fact(ConfigLoadTraceEntry::Kind::SkippedLine),
			fact(ConfigLoadTraceEntry::Kind::SetupError) });
		expectEqual(info.parseError, "first", "the first parse error wins");
		expectTrue(info.lineSkipped, "a skipped line is flagged");
		expectTrue(info.setupFailed, "a setup failure is flagged");
	}

	// Scope roles: an If line is a head even outside any scope; branch, tail
	// and member rows need a scope to draw.
	using Role = CommandRowInfo::ScopeRole;
	expectTrue(CommandRowFacts::scopeRole("if", "if", 0) == Role::Head, "an If line is a head");
	expectTrue(CommandRowFacts::scopeRole("if", "elseif", 1) == Role::Branch, "an ElseIf line is a branch");
	expectTrue(CommandRowFacts::scopeRole("if", "else", 2) == Role::Branch, "an Else line is a branch");
	expectTrue(CommandRowFacts::scopeRole("if", "endif", 1) == Role::Tail, "an EndIf line is a tail");
	expectTrue(CommandRowFacts::scopeRole("preamp", "preamp", 1) == Role::Member, "a row inside a scope is a member");
	expectTrue(CommandRowFacts::scopeRole("preamp", "preamp", 0) == Role::None, "a row outside any scope has no role");
	expectTrue(CommandRowFacts::scopeRole("if", "endif", 0) == Role::None, "a stray EndIf has no scope to draw");

	// Lane and scope geometry. A skin that indents siblings as members puts
	// an ElseIf one unit past its semantic depth; the channel bands are the
	// ones outside the If lanes.
	CommandRowFacts::RowContext context;
	context.laneUnit = 18;
	context.rowHeight = 31;
	context.logicSiblingsIndentAsMembers = true;
	context.selected = true;

	const CommandRowInfo member = CommandRowFacts::describe(scoped("Preamp: -3 dB", 2, 1), context, {});
	expectTrue(member.scopeRole == Role::Member, "a scoped Preamp row is a member");
	expectEqual(member.laneCount, 2, "a member draws its depth in lanes");
	expectEqual(member.channelLaneCount, 1, "one channel band outside one If lane");
	expectEqual(member.cardLeft, CommandRowInfo::cardMarginLeft + 2 * 18, "the card face starts after the lanes");
	expectEqual(member.laneOrigin(), CommandRowInfo::cardMarginLeft, "the lanes start at the card margin");
	expectEqual(member.laneCenter(0), CommandRowInfo::cardMarginLeft + 9, "a lane centre is half a band in");
	expectTrue(member.headerCenterY == CommandRowInfo::cardMarginTop + 15.5, "the header centre keeps the half pixel");
	expectEqual(static_cast<int>(member.headerCenterY), CommandRowInfo::cardMarginTop + 31 / 2,
		"truncating the header centre gives the pixel-grid row");
	expectTrue(member.selected && !member.focused, "selection comes from the context");
	expectEqual(member.command, "preamp", "the command is lower-cased");

	const CommandRowInfo branch = CommandRowFacts::describe(scoped("ElseIf: x", 1, 2), context, {});
	expectTrue(branch.scopeRole == Role::Branch, "an ElseIf row is a branch");
	expectEqual(branch.laneCount, 2, "a branch indents with the members");
	expectEqual(branch.channelLaneCount, 0, "a branch's lanes are all If lanes");
	context.logicSiblingsIndentAsMembers = false;
	expectEqual(CommandRowFacts::describe(scoped("ElseIf: x", 1, 2), context, {}).laneCount, 1,
		"a branch keeps its semantic depth when the skin aligns it with its head");
	expectEqual(CommandRowFacts::indentUnits(scoped("If: x", 1, 1), true), 1,
		"an If head never takes the extra unit");

	const CommandRowInfo head = CommandRowFacts::describe(scoped("If: x", 0, 0), context,
		{ condition(ConfigLoadTraceEntry::Result::True) });
	expectTrue(head.scopeRole == Role::Head, "an If row is a head");
	expectEqual(head.branchState, 1, "describe folds the load facts in");
}
