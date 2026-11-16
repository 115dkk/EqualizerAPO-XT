/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Tiny, header-only, framework-free assertion harness shared by the test
	suites (HybridConvTests, AudioRegressionTests, EditorLogicTests,
	EngineOrchestrationTests, AsioTests). It keeps the common assertion
	primitives in one place so the suites do not each carry their own copy of
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
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Tests/AlignedMemoryGate.h"

namespace test
{

// How a failed expect*() is handled; see the file comment. require*() and
// fail() abort under either policy.
enum class FailurePolicy
{
	Abort,
	Collect
};

// Absolute-tolerance comparison behind Harness::expectNear, for the call sites
// that fold several comparisons into one check (a && b, or a loop that
// accumulates a verdict) and so cannot call expectNear once per value. A NaN
// on either side is never near anything.
inline bool nearlyEqual(double actual, double expected, double tolerance)
{
	const double diff = actual > expected ? actual - expected : expected - actual;
	return diff <= tolerance;
}

// Counts assertions that passed (and, under Collect, failed) within a single
// suite and labels failure output with the suite name. runAll() creates one
// instance for every registered suite.
class Harness
{
public:
	explicit Harness(std::string suiteName, FailurePolicy policy = FailurePolicy::Collect)
		: name_(std::move(suiteName)), policy_(policy), passed_(0), failed_(0), skipped_(0),
		caseSkipped_(false), reported_(false)
	{
	}

	// Backstop for the Collect default. A suite that records failures and then
	// leaves without report() would exit 0 and turn a broken test green; that
	// cannot be caught at compile time, so catch it here instead. runAll()
	// owns the harnesses, so this can run during exception unwinding where a
	// second exception is not allowed. _Exit is safe after a manual flush.
	//
	// The zero-check case is a failure too (audit #275 TD-10/D4). runAll()
	// owns every registered suite harness, so this backstop now applies to
	// every suite. Suites that legitimately run nothing (soft skips) still
	// reach report(), which keeps them out of this branch.
	~Harness()
	{
		if (reported_)
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
			std::fprintf(stderr, "%s ran zero checks and never called report()."
				" Its registered cases did not complete; the suite must not pass silently.\n",
				name_.c_str());
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
	// NaN on either side fails, because nearlyEqual is then false.
	void expectNear(double actual, double expected, double tolerance, const std::string& message)
	{
		if (!nearlyEqual(actual, expected, tolerance))
		{
			// Twelve digits, so a miss by less than a millionth still shows
			// which digit differs.
			std::ostringstream oss;
			oss.precision(12);
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

	// runAll calls this before each registered case. skip() marks the current
	// case once, so a case that omits more than one permission-dependent check
	// still contributes one skipped case to the suite summary.
	void beginCase()
	{
		caseSkipped_ = false;
	}

	void skip(const std::string& reason)
	{
		if (!caseSkipped_)
		{
			++skipped_;
			caseSkipped_ = true;
		}
		std::printf("%s skipped: %s\n", name_.c_str(), reason.c_str());
		std::fflush(stdout);
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
			std::exit(1);
		}
		if (skipped_ == 0)
			std::printf("%s passed (%u checks)\n", name_.c_str(), passed_);
		else
			std::printf("%s passed (%u checks, %u skipped)\n", name_.c_str(), passed_, skipped_);
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
	unsigned skipped_;
	bool caseSkipped_;
	bool reported_;
};

using TestFunction = void (*)(Harness&);
using CleanupFunction = void (*)();

struct TestCase
{
	const char* name;
	TestFunction function;
};

struct TestSuite
{
	std::string name;
	std::vector<TestCase> cases;
	std::vector<CleanupFunction> cleanups;
};

inline std::vector<TestSuite>& registeredSuites()
{
	static std::vector<TestSuite> suites;
	return suites;
}

inline TestSuite& registeredSuite(const char* name)
{
	for (TestSuite& suite : registeredSuites())
	{
		if (suite.name == name)
			return suite;
	}
	registeredSuites().push_back({name, {}, {}});
	return registeredSuites().back();
}

inline std::vector<std::string>& executedSuiteNames()
{
	static std::vector<std::string> names;
	return names;
}

inline std::string& registeredSuiteBinaryName()
{
	static std::string name;
	return name;
}

inline bool suiteNameWasExecuted(const std::string& name)
{
	for (const std::string& executed : executedSuiteNames())
	{
		if (executed == name)
			return true;
	}
	return false;
}

// A hand-written runAll order must not silently omit a newly registered suite.
// Check at process exit so executables that make more than one runAll call are
// covered as a whole (audit #467 TD-65).
inline void verifyEveryRegisteredSuiteRan()
{
	bool missing = false;
	for (const TestSuite& suite : registeredSuites())
		missing = missing || !suiteNameWasExecuted(suite.name);
	if (!missing)
		return;

	const std::string& binaryName = registeredSuiteBinaryName();
	std::fprintf(stderr, "%s did not run these registered test suites:",
		binaryName.empty() ? "The test executable" : binaryName.c_str());
	for (const TestSuite& suite : registeredSuites())
	{
		if (!suiteNameWasExecuted(suite.name))
			std::fprintf(stderr, " %s", suite.name.c_str());
	}
	std::fputc('\n', stderr);
	std::fflush(nullptr);
	std::_Exit(1);
}

inline void registerSuiteCoverageCheck()
{
	// Construct these before registering the callback so their destructors run
	// after the callback and its reads remain valid.
	(void)executedSuiteNames();
	(void)registeredSuiteBinaryName();
	static const bool registered = std::atexit(verifyEveryRegisteredSuiteRan) == 0;
	if (!registered)
	{
		std::fputs("Could not register the test-suite coverage check.\n", stderr);
		std::fflush(nullptr);
		std::_Exit(1);
	}
}

inline void markSuiteExecuted(const char* binaryName, const std::string& suiteName)
{
	registeredSuiteBinaryName() = binaryName;
	if (!suiteNameWasExecuted(suiteName))
		executedSuiteNames().push_back(suiteName);
}

class TestRegistrar
{
public:
	TestRegistrar(const char* suite, const char* name, TestFunction function)
	{
		registeredSuite(suite).cases.push_back({name, function});
		registerSuiteCoverageCheck();
	}
};

class CleanupRegistrar
{
public:
	CleanupRegistrar(const char* suite, CleanupFunction function)
	{
		registeredSuite(suite).cleanups.push_back(function);
		registerSuiteCoverageCheck();
	}
};

inline Harness*& activeHarnessStorage()
{
	static Harness* harness = nullptr;
	return harness;
}

inline Harness& activeHarness()
{
	Harness* harness = activeHarnessStorage();
	if (harness == nullptr)
	{
		std::fputs("A test assertion ran outside test::runAll().\n", stderr);
		std::fflush(nullptr);
		std::_Exit(1);
	}
	return *harness;
}

// Lets helpers shared by several TEST_CASE bodies retain the concise
// harness.expect* spelling. A TEST_CASE parameter shadows this proxy inside the
// registered body itself; helpers resolve it through activeHarness().
class HarnessAccess
{
public:
	operator Harness&() const
	{
		return activeHarness();
	}

	[[noreturn]] static void fail(const std::string& message)
	{
		activeHarness().fail(message);
	}

	static void expect(bool condition, const std::string& message)
	{
		activeHarness().expect(condition, message);
	}

	static void expectTrue(bool condition, const std::string& message)
	{
		activeHarness().expectTrue(condition, message);
	}

	static void expectFalse(bool condition, const std::string& message)
	{
		activeHarness().expectFalse(condition, message);
	}

	template<typename A, typename B>
	static void expectEqual(const A& actual, const B& expected, const std::string& message)
	{
		activeHarness().expectEqual(actual, expected, message);
	}

	static void expectNear(double actual, double expected, double tolerance, const std::string& message)
	{
		activeHarness().expectNear(actual, expected, tolerance, message);
	}

	static void skip(const std::string& reason)
	{
		activeHarness().skip(reason);
	}

	static void require(bool condition, const std::string& message)
	{
		activeHarness().require(condition, message);
	}

	template<typename A, typename B>
	static void requireEqual(const A& actual, const B& expected, const std::string& message)
	{
		activeHarness().requireEqual(actual, expected, message);
	}
};

inline int runAll(const char* binaryName,
	std::initializer_list<const char*> suiteOrder = {}, CleanupFunction afterAll = nullptr)
{
	std::vector<TestSuite*> orderedSuites;
	if (suiteOrder.size() == 0)
	{
		for (TestSuite& suite : registeredSuites())
			orderedSuites.push_back(&suite);
	}
	else
	{
		for (const char* name : suiteOrder)
		{
			TestSuite* found = nullptr;
			for (TestSuite& suite : registeredSuites())
			{
				if (suite.name == name)
				{
					found = &suite;
					break;
				}
			}
			if (found == nullptr)
			{
				std::fprintf(stderr, "%s has no registered suite named %s.\n", binaryName, name);
				return 1;
			}
			orderedSuites.push_back(found);
		}
	}

	if (orderedSuites.empty())
	{
		std::fprintf(stderr, "%s has no registered tests.\n", binaryName);
		return 1;
	}

	std::vector<std::unique_ptr<Harness>> harnesses;
	for (TestSuite* suite : orderedSuites)
	{
		markSuiteExecuted(binaryName, suite->name);
		auto harness = std::make_unique<Harness>(suite->name);
		activeHarnessStorage() = harness.get();
		for (const TestCase& testCase : suite->cases)
		{
			harness->beginCase();
			std::printf("%s::%s\n", suite->name.c_str(), testCase.name);
			std::fflush(stdout);
			testCase.function(*harness);
		}
		for (CleanupFunction cleanup : suite->cleanups)
			cleanup();
		activeHarnessStorage() = nullptr;
		harnesses.push_back(std::move(harness));
	}

	if (afterAll != nullptr)
		afterAll();
	for (const std::unique_ptr<Harness>& harness : harnesses)
		harness->report();
	return reportAlignedMemoryBalance(binaryName);
}

} // namespace test

#define TEST_CASE(suite, name) \
	static void name(test::Harness& harness); \
	static const test::TestRegistrar testRegistrar_##name(#suite, #name, name); \
	static void name(test::Harness& harness)

#define TEST_SUITE_CLEANUP(suite, name) \
	static void name(); \
	static const test::CleanupRegistrar cleanupRegistrar_##name(#suite, name); \
	static void name()
