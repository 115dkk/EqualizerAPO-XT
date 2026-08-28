/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Tiny, header-only, framework-free assertion harness shared by the test
	suites (HybridConvTests, AudioRegressionTests, EditorLogicTests,
	EngineOrchestrationTests). It keeps the common assertion primitives in one
	place so the suites do not each carry their own copy of
	fail()/expect()/expectEqual().

	The harness deliberately avoids Qt and any heavy dependency so it can be
	included from a plain console test as well as from the Qt-linked
	EditorLogicTests. Messages are std::string; callers that work in another
	string type convert at the boundary.

	Two failure policies exist; the constructor takes Collect unless a suite
	asks for the other one.

	- Collect (the default): a failed expect*() prints to stderr, increments
	  the failure counter and lets the suite continue; report() then prints a
	  failure summary to stderr and exits with code 1. It is the default
	  because under Abort a failure at the top of a suite hides every finding
	  below it, costing one CI round-trip per finding.
	- Abort (opt-in): a failed assertion prints the same stderr line and exits
	  the process with code 1 immediately.

	Under Collect only report() fails the build, so every path that leaves a
	suite - including soft skips and early returns - has to reach it. A suite
	that cannot guarantee that must pass Abort explicitly. The destructor is
	the backstop: a harness that recorded failures and was never reported kills
	the process with code 1 rather than letting the run finish green.

	The require*() family always aborts on failure regardless of the policy.
	It is for gating checks whose failure would make the following code unsafe
	under Collect: size checks before indexing, null checks before
	dereferencing, parse-success checks before field access. fail() likewise
	always aborts.
*/

#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace test
{

// How a failed expect*() is handled; see the file comment. require*() and
// fail() abort under either policy.
enum class FailurePolicy
{
	Abort,
	Collect
};

// Counts assertions that passed (and, under Collect, failed) within a single
// suite and labels failure output with the suite name. One instance is
// created in each suite's main().
class Harness
{
public:
	explicit Harness(std::string suiteName, FailurePolicy policy = FailurePolicy::Collect)
		: name_(std::move(suiteName)), policy_(policy), passed_(0), failed_(0), reported_(false)
	{
	}

	// Backstop for the Collect default. A suite that records failures and then
	// leaves without report() would exit 0 and turn a broken test green; that
	// cannot be caught at compile time, so catch it here instead. Suite
	// harnesses live at namespace scope, so this often runs during static
	// destruction where std::exit is not allowed - _Exit is, after a manual
	// flush. aborting_ keeps an in-flight fail() from tripping the sibling
	// harnesses that never got their turn to report.
	//
	// The zero-check case is a failure too (audit #275 TD-10/D4): the suite
	// binaries assemble their sub-suites with a hand-maintained
	// declaration/call list, so a runXxxTests() that is declared, compiled and
	// linked but never called used to finish green with zero checks. Its
	// namespace-scope harness still constructs and destructs, which is where
	// this catches it. Suites that legitimately run nothing (soft skips) call
	// report(), which keeps them out of this branch.
	~Harness()
	{
		if (reported_ || aborting_)
			return;
		if (failed_ != 0)
		{
			std::fprintf(stderr, "%s FAILED (%u of %u checks failed) and never called report()\n",
				name_.c_str(), failed_, passed_ + failed_);
			std::fflush(nullptr);
			std::_Exit(1);
		}
		if (passed_ == 0)
		{
			std::fprintf(stderr, "%s is linked into this binary but ran zero checks and never called report()."
				" Its runner is probably missing from the suite's main(); a compiled-but-uncalled"
				" suite must not pass silently.\n", name_.c_str());
			std::fflush(nullptr);
			std::_Exit(1);
		}
	}

	// Prints the failure to stderr and terminates with exit code 1, matching
	// the behaviour of the per-suite fail() helpers it replaces. Aborts under
	// either policy.
	[[noreturn]] void fail(const std::string& message) const
	{
		std::fprintf(stderr, "%s failed: %s\n", name_.c_str(), message.c_str());
		aborting_ = true;
		std::exit(1);
	}

	// Generic boolean assertion. expectTrue is provided as an explicit alias
	// because some suites read better with it.
	void expect(bool condition, const std::string& message)
	{
		if (!condition)
		{
			recordFailure(message);
			return;
		}
		++passed_;
	}

	void expectTrue(bool condition, const std::string& message)
	{
		expect(condition, message);
	}

	void expectFalse(bool condition, const std::string& message)
	{
		expect(!condition, message);
	}

	// Templated equality check. Works for any types comparable with != that
	// can be streamed into an ostringstream for the diagnostic message.
	template<typename A, typename B>
	void expectEqual(const A& actual, const B& expected, const std::string& message)
	{
		if (!(actual == expected))
		{
			std::ostringstream oss;
			oss << message << ": expected '" << expected << "', got '" << actual << "'";
			recordFailure(oss.str());
			return;
		}
		++passed_;
	}

	// Approximate floating-point equality with an absolute tolerance (audit
	// #275 D5/TD-23: the suites carried three private variants of this). A
	// NaN on either side fails, because !(diff <= tolerance) is then true.
	void expectNear(double actual, double expected, double tolerance, const std::string& message)
	{
		const double diff = actual > expected ? actual - expected : expected - actual;
		if (!(diff <= tolerance))
		{
			std::ostringstream oss;
			oss << message << ": expected '" << expected << "' within " << tolerance
				<< ", got '" << actual << "'";
			recordFailure(oss.str());
			return;
		}
		++passed_;
	}

	// Gating assertion: aborts on failure regardless of the policy. Use it
	// when the failure would make the following code unsafe (a size check
	// before indexing, a null check before dereferencing). On success it
	// counts as a passed check like the expect family.
	void require(bool condition, const std::string& message)
	{
		if (!condition)
			fail(message);
		++passed_;
	}

	// Gating counterpart of expectEqual: same diagnostic format, but aborts
	// on failure regardless of the policy.
	template<typename A, typename B>
	void requireEqual(const A& actual, const B& expected, const std::string& message)
	{
		if (!(actual == expected))
		{
			std::ostringstream oss;
			oss << message << ": expected '" << expected << "', got '" << actual << "'";
			fail(oss.str());
		}
		++passed_;
	}

	unsigned passed() const
	{
		return passed_;
	}

	// Carries the verdict, so every suite has to reach it before it leaves:
	// under Collect this is the only place a recorded failure turns into a
	// non-zero exit code. On failure it prints a summary to stderr and exits
	// with code 1; otherwise it emits a single "<suite> passed (<n> checks)"
	// line on stdout, which a suite with its own success banner may duplicate.
	void report()
	{
		reported_ = true;
		if (failed_ > 0)
		{
			std::fprintf(stderr, "%s FAILED (%u of %u checks failed)\n", name_.c_str(), failed_, passed_ + failed_);
			aborting_ = true;
			std::exit(1);
		}
		std::printf("%s passed (%u checks)\n", name_.c_str(), passed_);
	}

private:
	// Routes an expect*() failure according to the policy: Abort exits via
	// fail(), Collect prints the identical stderr line and lets the suite
	// continue so later findings still surface.
	void recordFailure(const std::string& message)
	{
		if (policy_ == FailurePolicy::Abort)
			fail(message);
		std::fprintf(stderr, "%s failed: %s\n", name_.c_str(), message.c_str());
		++failed_;
	}

	std::string name_;
	FailurePolicy policy_;
	unsigned passed_;
	unsigned failed_;
	bool reported_;

	// Set once the process is already on its way out with a non-zero code, so
	// the destructor backstop stays quiet for every other harness in the same
	// binary. Written from the const fail(), hence mutable-by-being-static.
	static inline bool aborting_ = false;
};

} // namespace test
