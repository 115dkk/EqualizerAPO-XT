/*
	This file is part of EqualizerAPO-XT.

	Contract tests for the command-classification seam in
	filters/FilterFactoryRegistry: knownConfigCommands() and above all
	canonicalCommand(), the one routine that answers "is this configuration line a
	command, and which one" for the engine, the Editor's card model and the
	card-editor registry. Those three used to answer it
	separately and drifted apart, so the rule is pinned here rather than in any
	one consumer.

	Two properties of canonicalCommand carry the weight:

	  - The answer follows what the factories actually do. Keys beginning with
	    "Filter" resolve to "Filter" because BiQuad and IIR match by prefix, so
	    "Filter 1" and "Filter1" are both Filter commands. Every other keyword is
	    compared whole, so "Channel 2" is not the Channel command and the engine
	    never runs it.
	  - The match is case-sensitive. Equalizer APO 1.4.2 leaves a line such as
	    "copy: remember to re-measure the room" inert precisely because "copy" is
	    not "Copy", so a config that has carried that note for years has to keep
	    behaving as plain text. Accepting either casing would start routing audio
	    in configurations that never did.

	Everything here is pure logic over the registry, so this suite needs no audio
	device and links the same Common.lib as the rest of the binary. The
	/WHOLEARCHIVE:Common.lib link option is what drags every self-registering
	factory translation unit into the link, which is why the sweep over
	knownConfigCommands() below sees the full vocabulary instead of an empty set.
*/

#include <set>
#include <string>

#include "filters/FilterFactoryRegistry.h"
#include "text/StringHelper.h"
#include "Tests/TestHarness.h"

using std::set;
using std::string;
using std::wstring;

namespace
{
test::Harness harness("FilterFactoryRegistryTests");

// The harness builds its diagnostics in a narrow ostringstream, which cannot
// take a wstring. Every key this suite feeds canonicalCommand is ASCII, so a
// byte-wise narrowing is enough to keep the failing key readable in the report.
string narrow(const wstring& text)
{
	string result;
	result.reserve(text.size());
	for (wchar_t character : text)
		result.push_back(character < 128 ? static_cast<char>(character) : '?');
	return result;
}

// Asserts canonicalCommand(key), quoting the key, the expectation and the
// actual answer so a failure names the input without a debugger. why states the
// rule the case exists for, because these expectations are only obvious once the
// 1.4.2 compatibility argument is in front of the reader.
void expectCanonical(const wstring& key, const wstring& expected, const string& why)
{
	const wstring actual = FilterFactoryRegistry::canonicalCommand(key);
	harness.expectTrue(actual == expected,
		"canonicalCommand(\"" + narrow(key) + "\") should be \"" + narrow(expected)
		+ "\" but was \"" + narrow(actual) + "\"; " + why);
}

void testExactSpellingIsItsOwnCanonicalForm()
{
	expectCanonical(L"Preamp", L"Preamp", "an exactly spelled keyword canonicalizes to itself");
	expectCanonical(L"Copy", L"Copy", "an exactly spelled keyword canonicalizes to itself");
	expectCanonical(L"Filter", L"Filter", "a bare \"Filter\" key is already the canonical form");
	expectCanonical(L"GraphicEQ", L"GraphicEQ", "internal capitalization is part of the keyword");
	expectCanonical(L"MultiConvolution", L"MultiConvolution", "the longest keyword is matched whole, not as a prefix of Convolution");
	expectCanonical(L"EndIf", L"EndIf", "control-flow keywords are classified like any other command");
}

void testCasingDecidesCommandVersusProse()
{
	// This is the 1.4.2 compatibility rule. A user's config may contain a line
	// like "copy: swapped the left and right cables" that has always been inert
	// text; recognizing it would silently start duplicating a channel.
	expectCanonical(L"preamp", wstring(), "lower-case \"preamp\" is prose, not a command, exactly as in Equalizer APO 1.4.2");
	expectCanonical(L"PREAMP", wstring(), "upper-case \"PREAMP\" is prose, not a command, exactly as in Equalizer APO 1.4.2");
	expectCanonical(L"PreAmp", wstring(), "a keyword with different internal casing is prose, not a command");
	expectCanonical(L"copy", wstring(), "\"copy: a note to self\" stays an inert comment, which is why the match is case-sensitive");
	expectCanonical(L"COPY", wstring(), "\"COPY\" would route audio if casing were ignored, so it must not be recognized");
	expectCanonical(L"filter", wstring(), "lower-case \"filter\" is prose, not a command");
	expectCanonical(L"if", wstring(), "lower-case \"if\" must not start a conditional block");
	expectCanonical(L"Endif", wstring(), "\"Endif\" is not the registered spelling \"EndIf\"");
	expectCanonical(L"graphiceq", wstring(), "lower-case \"graphiceq\" is prose, not a command");
	expectCanonical(L"GraphicEq", wstring(), "\"GraphicEq\" is not the registered spelling \"GraphicEQ\"");
}

void testTrailingTokenBelongsToTheFactory()
{
	expectCanonical(L"Filter 1", L"Filter", "the filter number is the factory's business, not part of the keyword");
	expectCanonical(L"Filter  12", L"Filter", "repeated separators before the trailing token do not change the keyword");
	expectCanonical(L"Filter\t3", L"Filter", "a tab separates the keyword from the trailing token just like a space");

	// A trailing token is grammar only for the Filter family. Every other
	// factory compares the whole key, so "Copy something" is a key nothing
	// claims and the engine runs no filter for it. Reporting it as Copy would
	// invite the Editor to rewrite the line into a Copy command that then does
	// route audio.
	expectCanonical(L"Copy something", wstring(), "only the Filter family takes a trailing token; every other factory compares the whole key");
	expectCanonical(L"Channel 2", wstring(), "\"Channel 2\" is not the Channel command; ChannelFilterFactory compares the whole key");
	expectCanonical(L"Device 2", wstring(), "\"Device 2\" is not the Device command either");
}

void testSurroundingWhitespaceIsIgnored()
{
	// FilterEngine::loadConfigFile trims the key before calling, but the Editor
	// side hands over text straight from the line, so the function has to
	// survive the untrimmed form on its own.
	expectCanonical(L"  Preamp", L"Preamp", "leading indentation is not part of the keyword");
	expectCanonical(L"Preamp  ", L"Preamp", "trailing spaces are not part of the keyword");
	expectCanonical(L"\tCopy\t", L"Copy", "tabs are treated as whitespace on both sides");
	expectCanonical(L"  Filter 1  ", L"Filter", "trimming and the first-token rule compose");
}

void testPrefixMatchingFollowsTheFactories()
{
	// The Filter family really is prefix-matched in the engine: BiQuadFilterFactory
	// and IIRFilterFactory both test rfind(L"Filter", 0) == 0, and IIRCommandTests
	// pins the unspaced "Filter1" form. Answering anything else here would make the
	// Editor show plain text for a line the engine turns into a filter.
	expectCanonical(L"Filter1", L"Filter", "\"Filter1\" reaches the BiQuad and IIR factories, so it is a Filter command");
	expectCanonical(L"Filterfoo", L"Filter", "the factories match by prefix, however odd the suffix looks");

	// No other keyword is prefix-matched, so a longer word merely starting with
	// one is not that command.
	expectCanonical(L"Preamplifier", wstring(), "a longer word starting with \"Preamp\" is not the Preamp command");
	expectCanonical(L"Copying", wstring(), "a longer word starting with \"Copy\" is not the Copy command");
	expectCanonical(L"Pre", wstring(), "a proper prefix of a keyword is not a command either");
}

void testEmptyAndUnregisteredKeys()
{
	expectCanonical(wstring(), wstring(), "an empty key claims no command");
	expectCanonical(L"   ", wstring(), "a whitespace-only key has no first token, so it claims no command");
	expectCanonical(L"\t \t", wstring(), "tabs and spaces alike leave no first token");
	expectCanonical(L"Volume", wstring(), "an unregistered keyword claims no command");
	expectCanonical(L"Reverb", wstring(), "an unregistered keyword claims no command");
	expectCanonical(L"note", wstring(), "\"note: ...\" is the ordinary way users leave comments with a colon in them");
}

// Sweeps the whole registered vocabulary so a filter added later inherits this
// coverage without anyone remembering to extend the case list above.
void testEveryRegisteredKeywordRoundTrips()
{
	const set<wstring>& commands = FilterFactoryRegistry::knownConfigCommands();

	// A vacuous loop would report success. An empty set is exactly what a link
	// that lost /WHOLEARCHIVE:Common.lib produces, since nothing names the
	// self-registering factory translation units, so gate the sweep on the
	// vocabulary actually being there.
	harness.require(!commands.empty(), "knownConfigCommands() must not be empty; the factory TUs were dropped from the link");
	harness.require(commands.count(L"Preamp") == 1 && commands.count(L"Filter") == 1 && commands.count(L"Copy") == 1,
		"knownConfigCommands() must contain the core Preamp/Filter/Copy keywords");

	for (const wstring& keyword : commands)
	{
		expectCanonical(keyword, keyword, "every registered keyword is its own canonical form");
		expectCanonical(L"  " + keyword + L"\t", keyword, "surrounding whitespace never changes the verdict");

		// Only the Filter family is prefix-matched by its factories, so only
		// there does a trailing token still name the command. For every other
		// keyword the factory compares the whole key, and reporting "Channel 1"
		// as Channel would let the Editor rewrite a line the engine ignores into
		// one it runs.
		if (keyword == L"Filter")
			expectCanonical(keyword + L" 1", keyword, "BiQuad and IIR match by prefix, so \"Filter 1\" is still a Filter command");
		else
			expectCanonical(keyword + L" 1", wstring(), "a trailing token makes this a key no factory claims");

		// Case folding is checked against the whole vocabulary rather than a
		// hand-picked few, so the 1.4.2 rule cannot be lost by adding a filter.
		// The guards skip a keyword that is already single-case or whose folded
		// form is itself registered, which would make the expectation wrong
		// rather than merely unmet.
		const wstring lowered = StringHelper::toLowerCase(keyword);
		if (lowered != keyword && commands.count(lowered) == 0)
			expectCanonical(lowered, wstring(), "a lower-cased keyword is prose, the way 1.4.2 leaves \"copy: a note\" inert");

		const wstring uppered = StringHelper::toUpperCase(keyword);
		if (uppered != keyword && commands.count(uppered) == 0)
			expectCanonical(uppered, wstring(), "an upper-cased keyword is prose for the same 1.4.2 reason");
	}
}

}

void runFilterFactoryRegistryTests()
{
	testExactSpellingIsItsOwnCanonicalForm();
	testCasingDecidesCommandVersusProse();
	testTrailingTokenBelongsToTheFactory();
	testSurroundingWhitespaceIsIgnored();
	testPrefixMatchingFollowsTheFactories();
	testEmptyAndUnregisteredKeys();
	testEveryRegisteredKeywordRoundTrips();

	harness.report();
}
