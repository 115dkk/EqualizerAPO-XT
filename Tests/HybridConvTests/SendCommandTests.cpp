/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <string>

#include "filters/SendCommand.h"
#include "Tests/TestHarness.h"

namespace
{
	test::Harness harness("SendCommandTests");
	const std::wstring endpointUpper = L"{A6974EEF-CBB1-4BD4-8E75-3F7D86F44E2F}";
	const std::wstring endpointLower = L"{a6974eef-cbb1-4bd4-8e75-3f7d86f44e2f}";

	bool parse(const std::wstring& parameters, SendCommand& command, std::wstring& error)
	{
		return SendCommand::parse(L"Send", parameters, command, &error);
	}

	void expectRejected(const std::wstring& parameters, const std::wstring& reasonPart,
		const std::string& label)
	{
		SendCommand command;
		std::wstring error;
		harness.expectFalse(parse(parameters, command, error), label + " is rejected");
		harness.expectTrue(error.find(reasonPart) != std::wstring::npos,
			label + " names the reason");
	}

	void testCommandAndEndpoint()
	{
		SendCommand command;
		std::wstring error = L"stale";
		harness.expectFalse(SendCommand::parse(L"send", endpointUpper + L" L=R", command, &error),
			"command matching is exact and case-sensitive");
		harness.expectTrue(error.empty(), "a non-Send command returns no parse error");

		harness.require(parse(endpointUpper + L" L=R", command, error),
			"uppercase canonical GUID input did not parse");
		harness.expectTrue(command.endpoint == endpointLower,
			"uppercase endpoint GUID canonicalizes to lower case");
		harness.expectTrue(SendCommand::canonicalEndpoint(endpointUpper) == endpointLower,
			"canonicalEndpoint returns the lower-case braced GUID");
		harness.expectTrue(SendCommand::canonicalEndpoint(endpointUpper.substr(1, 36)).empty(),
			"canonicalEndpoint rejects a GUID without braces");
		expectRejected(L"", L"endpoint GUID", "Send with no parameters");
		expectRejected(L"not-a-guid L=R", L"brace-wrapped GUID", "invalid endpoint GUID");
	}

	void testAssignments()
	{
		SendCommand command;
		std::wstring error;
		harness.require(parse(endpointLower + L" L=0.5*L+0.5*R SUB=-3dB*L", command, error),
			"valid Copy assignment grammar did not parse");
		harness.expectEqual(command.assignments.size(), size_t(2), "two Send assignments parse");
		harness.expectTrue(command.assignments[0].targetChannel == L"L", "target spelling is preserved");
		harness.expectTrue(command.assignments[1].sourceSum[0].isDecibel,
			"Copy's dB summand grammar is preserved");

		expectRejected(endpointLower, L"at least one", "missing assignment");
		expectRejected(endpointLower + L" garbage", L"garbage", "token without equals");
		expectRejected(endpointLower + L" L=R=C", L"L=R=C", "malformed assignment");
		expectRejected(endpointLower + L" L=0", L"not constants", "constant summand");
		expectRejected(endpointLower + L" L=R L=C", L"Duplicate", "duplicate target");
		expectRejected(endpointLower + L" abcdefghijklmnopqrstuvwxyzabcdef=R", L"longer than 31",
			"target longer than 31 characters");

		std::wstring sixteen = endpointLower;
		for (int i = 0; i < 16; i++)
			sixteen += L" T" + std::to_wstring(i) + L"=L";
		harness.expectTrue(parse(sixteen, command, error), "sixteen assignments are accepted");
		expectRejected(sixteen + L" T16=L", L"at most 16", "seventeen assignments");

		const std::wstring target31(31, L'T');
		harness.expectTrue(parse(endpointLower + L" " + target31 + L"=L", command, error),
			"a 31-character target is accepted");

		// Exact comparison keeps differently-cased receiving channel names distinct.
		harness.expectTrue(parse(endpointLower + L" Left=L left=R", command, error),
			"target duplicate comparison is case-sensitive");
	}

	void testLatency()
	{
		SendCommand command;
		std::wstring error;
		harness.require(parse(endpointLower + L" L=R Latency=12.5", command, error),
			"bare latency did not parse");
		harness.expectTrue(command.latencyUnit == SendCommand::LatencyUnit::Milliseconds,
			"bare latency uses milliseconds");
		harness.expectEqual(command.latency, 12.5, "bare latency value");
		harness.expectTrue(command.serialize() == endpointLower + L" L=R Latency=12.5ms",
			"bare latency serializes with an explicit millisecond suffix");

		harness.require(parse(endpointLower + L" L=R latency=960SAMPLES", command, error),
			"sample latency did not parse case-insensitively");
		harness.expectTrue(command.latencyUnit == SendCommand::LatencyUnit::Samples,
			"sample latency unit");
		harness.expectEqual(command.latency, 960.0, "sample latency value");

		expectRejected(endpointLower + L" L=R Latency=1.5samples", L"integer",
			"fractional sample latency");
		expectRejected(endpointLower + L" L=R Latency=0", L"positive finite",
			"zero latency");
		expectRejected(endpointLower + L" L=R Latency=-1ms", L"positive finite",
			"negative latency");
		expectRejected(endpointLower + L" L=R Latency=nan", L"positive finite",
			"non-finite latency");
		expectRejected(endpointLower + L" L=R Latency=1ms latency=2ms", L"Latency given twice",
			"duplicate latency");
		harness.expectTrue(parse(endpointLower + L" L=R Latency=1MS", command, error),
			"millisecond suffix matching is case-insensitive");
	}

	void testOptions()
	{
		SendCommand command;
		std::wstring error;
		harness.require(parse(endpointLower + L" L=R Compensate=FALSE Mode=replace", command, error),
			"case-insensitive option values did not parse");
		harness.expectFalse(command.compensate, "Compensate=false parses");
		harness.expectTrue(command.mode == SendCommand::Mode::Replace, "Mode=replace parses");

		harness.require(parse(endpointLower + L" L=R compensate=1 mode=MIX", command, error),
			"numeric compensate and Mix did not parse");
		harness.expectTrue(command.compensate, "Compensate=1 parses as true");
		harness.expectTrue(command.mode == SendCommand::Mode::Mix, "Mode=Mix parses");

		expectRejected(endpointLower + L" L=R Compensate=maybe", L"true, false, 1, or 0",
			"invalid compensate");
		expectRejected(endpointLower + L" L=R Mode=copy", L"Mix or Replace", "invalid mode");
		expectRejected(endpointLower + L" L=R Compensate=true compensate=0", L"Compensate given twice",
			"duplicate compensate");
		expectRejected(endpointLower + L" L=R Mode=Mix mode=Replace", L"Mode given twice",
			"duplicate mode");
	}

	void testSerializeRoundTrip()
	{
		SendCommand command;
		std::wstring error;
		const std::wstring parameters = endpointUpper
			+ L" Left=0.5*L+-3dB*R Right=R Latency=12.500ms Compensate=0 Mode=replace";
		harness.require(parse(parameters, command, error), "round-trip source did not parse");
		const std::wstring serialized = command.serialize();
		harness.expectTrue(serialized == endpointLower
			+ L" Left=0.5*L+-3.0dB*R Right=R Latency=12.5ms Compensate=false Mode=Replace",
			"serialize emits the canonical Send spelling");

		SendCommand reparsed;
		harness.require(parse(serialized, reparsed, error), "serialized Send command did not parse");
		harness.expectTrue(reparsed.endpoint == command.endpoint, "round-trip endpoint");
		harness.expectTrue(serializeCopyAssignments(reparsed.assignments)
			== serializeCopyAssignments(command.assignments), "round-trip assignments");
		harness.expectTrue(reparsed.latencyUnit == command.latencyUnit, "round-trip latency unit");
		harness.expectEqual(reparsed.latency, command.latency, "round-trip latency value");
		harness.expectTrue(reparsed.compensate == command.compensate, "round-trip compensate");
		harness.expectTrue(reparsed.mode == command.mode, "round-trip mode");

		harness.require(parse(endpointLower + L" L=R Latency=960samples", command, error),
			"sample serialization source did not parse");
		harness.expectTrue(command.serialize() == endpointLower + L" L=R Latency=960samples",
			"sample latency serializes as an integer with its unit");

		harness.require(parse(endpointLower + L" L=R", command, error),
			"default serialization source did not parse");
		harness.expectTrue(command.serialize() == endpointLower + L" L=R",
			"default latency, compensate, and mode are omitted");
	}
}

void runSendCommandTests()
{
	testCommandAndEndpoint();
	testAssignments();
	testLatency();
	testOptions();
	testSerializeRoundTrip();
	harness.report();
}
