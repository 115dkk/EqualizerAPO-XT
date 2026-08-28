/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

    Per-line facts collected while FilterEngine loads a configuration, so a UI
    (the Editor) can echo evaluation results next to the config rows: which If
    branch ran, what an Eval computed, which lines a false branch swallowed.
    Collection is off unless a sink is attached via
    FilterEngine::setLoadTraceSink; the APO runtime never attaches one, so the
    audio service pays nothing beyond a null check per reported event.
*/

#pragma once

#include <string>

struct ConfigLoadTraceEntry
{
	enum class Kind
	{
		// An If/ElseIf line. result says what happened to the condition;
		// active mirrors result == Result::True.
		Condition,
		// An Else line: active says whether the else-branch body executes.
		// result stays NotEvaluated (an Else has no expression).
		ElseBranch,
		// An Eval line: text carries the expression result, or the parser
		// message when error is set.
		Eval,
		// A line with inline `expression` segments: text carries the fully
		// substituted parameter string the downstream factories saw.
		InlineValue,
		// Any line a false branch swallowed before the other factories saw
		// it. Comment lines are not reported (they never execute anyway).
		SkippedLine,
		// A line whose command was recognised but whose parameters could not be
		// parsed, reported by the factory that owns the command. text carries the
		// reason and error is always set.
		//
		// This kind replaced a guess. The engine used to notice that a recognised
		// command had produced no filter and log "likely due to malformed
		// parameters", with a hand-maintained list of commands that legitimately
		// produce none - which meant the diagnosis was inferred from the outside by
		// the one place that cannot know why a parse failed, and a new filter with a
		// valid no-op path silently became a false warning until somebody added it
		// to the list.
		ParseError,
	};

	// How a Condition line's expression fared. NotEvaluated marks an ElseIf
	// whose chain was already satisfied (the engine short-circuits without
	// evaluating) and the ElseIf/Else-without-If misuse cases.
	enum class Result
	{
		True,
		False,
		NotEvaluated,
		Error,
	};

	// Absolute path of the config file the line is in (as the engine opened
	// it) and the 1-based line number within that file. Stamped by
	// FilterEngine::traceLoadEvent; factories fill only the fields below.
	std::wstring file;
	int line = 0;

	Kind kind = Kind::SkippedLine;
	Result result = Result::NotEvaluated;
	// Kind::Condition/ElseBranch: the branch body will execute.
	bool active = false;
	// Kind::Eval/InlineValue payload, or the parser error message.
	std::wstring text;
	bool error = false;
};

// Receives entries during FilterEngine::loadConfig. Calls arrive on whichever
// thread loads the configuration. With a caller-supplied custom path the load
// runs synchronously inside initialize()/loadConfig() and no registry
// notification worker is started, so a plain single-threaded collector is
// safe there (the Editor's analysis engine is exactly that case).
class ConfigLoadTraceSink
{
public:
	virtual ~ConfigLoadTraceSink() = default;
	virtual void addEntry(const ConfigLoadTraceEntry& entry) = 0;
};
