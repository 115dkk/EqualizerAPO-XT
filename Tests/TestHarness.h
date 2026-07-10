/*
	This file is part of EqualizerAPO-XT.

	Tiny, header-only, framework-free assertion harness shared by the test
	suites (HybridConvTests, AudioRegressionTests, EditorLogicTests,
	EngineOrchestrationTests). It keeps the common assertion primitives in one
	place so the suites do not each carry their own copy of
	fail()/expect()/expectEqual().

	The harness deliberately avoids Qt and any heavy dependency so it can be
	included from a plain console test as well as from the Qt-linked
	EditorLogicTests. Messages are std::string; callers that work in another
	string type convert at the boundary.

	Two failure policies exist; each suite picks one in the constructor.

	- Abort (the default): a failed assertion prints to stderr and exits the
	  process with code 1. All suites behave this way unless they opt out.
	- Collect (opt-in): a failed expect*() prints the same stderr line,
	  increments the failure counter and lets the suite continue; report()
	  then prints a failure summary to stderr and exits with code 1. Collect
	  exists because under Abort a failure at the top of a suite hides every
	  finding below it, costing one CI round-trip per finding.

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
	explicit Harness(std::string suiteName, FailurePolicy policy = FailurePolicy::Abort)
		: name_(std::move(suiteName)), policy_(policy), passed_(0), failed_(0)
	{
	}

	// Prints the failure to stderr and terminates with exit code 1, matching
	// the behaviour of the per-suite fail() helpers it replaces. Aborts under
	// either policy.
	[[noreturn]] void fail(const std::string& message) const
	{
		std::fprintf(stderr, "%s failed: %s\n", name_.c_str(), message.c_str());
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

	// Emits a single "<suite> passed (<n> checks)" line on stdout. Suites that
	// already print their own success banner can call this instead, or keep
	// their banner and read passed() if they prefer. If Collect mode recorded
	// any failures, prints a failure summary to stderr instead and exits with
	// code 1 so the suite still fails the build.
	void report() const
	{
		if (failed_ > 0)
		{
			std::fprintf(stderr, "%s FAILED (%u of %u checks failed)\n", name_.c_str(), failed_, passed_ + failed_);
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
};

} // namespace test
