/*
	This file is part of EqualizerAPO-XT.

	Tiny, header-only, framework-free assertion harness shared by the test
	suites (HybridConvTests, AudioRegressionTests, EditorLogicTests). It keeps
	the common assertion primitives in one place so the suites no longer each
	carry their own copy of fail()/expect()/expectEqual().

	The harness deliberately avoids Qt and any heavy dependency so it can be
	included from a plain console test as well as from the Qt-linked
	EditorLogicTests. Messages are std::string; callers that work in another
	string type convert at the boundary.

	Semantics match the original hand-rolled helpers: a failed assertion prints
	to stderr and exits the process with code 1, so the existing pass/fail
	outcome of every suite is preserved.
*/

#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace test
{

// Counts assertions that passed within a single suite and labels failure
// output with the suite name. One instance is created in each suite's main().
class Harness
{
public:
	explicit Harness(std::string suiteName)
		: name_(std::move(suiteName)), passed_(0)
	{
	}

	// Prints the failure to stderr and terminates with exit code 1, matching
	// the behaviour of the per-suite fail() helpers it replaces.
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
			fail(message);
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
	// their banner and read passed() if they prefer.
	void report() const
	{
		std::printf("%s passed (%u checks)\n", name_.c_str(), passed_);
	}

private:
	std::string name_;
	unsigned passed_;
};

} // namespace test
