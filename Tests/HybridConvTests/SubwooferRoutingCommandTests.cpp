/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABSubwooferRoutingILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "filters/subwooferRouting/SubwooferRoutingCommand.h"
#include "Tests/TestHarness.h"

namespace
{
test::Harness harness("SubwooferRoutingCommandTests");

void testStateRoundTrip()
{
	const std::wstring payload =
		L"{ \"schema\": \"equalizerapo.xt.subwoofer-routing\", "
		L"\"version\": 1, \"note\": \"two  spaces\" }  ";
	SubwooferRoutingCommand command;
	std::wstring error;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L"State " + payload, command, &error);

	harness.expectTrue(parsed, "State form should parse");
	harness.expectEqual(
		static_cast<int>(command.form),
		static_cast<int>(SubwooferRoutingCommand::Form::State),
		"State form should be selected");
	harness.expectTrue(command.payload == payload,
		"State payload should preserve spaces, braces, quotes and trailing text");
	harness.expectTrue(command.serialize() == L"State " + payload,
		"State serialization should preserve the JSON payload");
}

void testQuotedProfile()
{
	const std::wstring payload =
		L"\"SubwooferRouting\\Living Room.swxt.json\"";
	SubwooferRoutingCommand command;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L"Profile " + payload, command);

	harness.expectTrue(parsed, "quoted Profile form should parse");
	harness.expectEqual(
		static_cast<int>(command.form),
		static_cast<int>(SubwooferRoutingCommand::Form::Profile),
		"Profile form should be selected");
	harness.expectTrue(command.payload == payload,
		"quoted Profile payload should preserve its quotes");
	harness.expectTrue(command.serialize() == L"Profile " + payload,
		"quoted Profile serialization should preserve its relative path");
}

void testUnquotedProfile()
{
	const std::wstring payload =
		L"SubwooferRouting\\LivingRoom.swxt.json";
	SubwooferRoutingCommand command;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L"Profile " + payload, command);

	harness.expectTrue(parsed, "unquoted Profile form should parse");
	harness.expectTrue(command.payload == payload,
		"unquoted relative Profile path should be preserved");
	harness.expectTrue(command.serialize() == L"Profile " + payload,
		"unquoted Profile serialization should preserve its relative path");
}

void testWrongTagError()
{
	SubwooferRoutingCommand command;
	std::wstring error;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L"Preset room.json", command, &error);

	harness.expectTrue(!parsed, "unknown tag should be rejected");
	harness.expectTrue(error == L"expected State or Profile",
		"unknown tag should report the expected forms");
}

void testEmptyParametersError()
{
	SubwooferRoutingCommand command;
	std::wstring error;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L" \t ", command, &error);

	harness.expectTrue(!parsed, "empty parameters should be rejected");
	harness.expectTrue(error == L"expected State or Profile",
		"empty parameters should report the expected forms");
}

void testTagWithoutPayloadError()
{
	SubwooferRoutingCommand command;
	std::wstring error;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L"State \t ", command, &error);

	harness.expectTrue(!parsed, "tag without a payload should be rejected");
	harness.expectTrue(error == L"expected State or Profile",
		"tag without a payload should report the expected forms");
}

void testLowercaseStateRejected()
{
	SubwooferRoutingCommand command;
	std::wstring error;
	const bool parsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", L"state {}", command, &error);

	harness.expectTrue(!parsed, "lowercase state tag should be rejected");
	harness.expectTrue(error == L"expected State or Profile",
		"lowercase state tag should report the expected forms");
}

void testSerializeRoundTrips()
{
	SubwooferRoutingCommand state;
	state.form = SubwooferRoutingCommand::Form::State;
	state.payload = L"{\"schema\":\"equalizerapo.xt.subwoofer-routing\",\"version\":1}";

	SubwooferRoutingCommand parsedState;
	const bool stateParsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", state.serialize(), parsedState);
	harness.expectTrue(stateParsed, "serialized State form should parse");
	harness.expectEqual(
		static_cast<int>(parsedState.form),
		static_cast<int>(state.form),
		"serialized State form should retain its form");
	harness.expectTrue(parsedState.payload == state.payload,
		"serialized State form should retain its payload");

	SubwooferRoutingCommand profile;
	profile.form = SubwooferRoutingCommand::Form::Profile;
	profile.payload = L"\"SubwooferRouting\\Living Room.swxt.json\"";

	SubwooferRoutingCommand parsedProfile;
	const bool profileParsed = SubwooferRoutingCommand::parse(
		L"SubwooferRouting", profile.serialize(), parsedProfile);
	harness.expectTrue(profileParsed, "serialized Profile form should parse");
	harness.expectEqual(
		static_cast<int>(parsedProfile.form),
		static_cast<int>(profile.form),
		"serialized Profile form should retain its form");
	harness.expectTrue(parsedProfile.payload == profile.payload,
		"serialized Profile form should retain its payload");
}
}

void runSubwooferRoutingCommandTests()
{
	testStateRoundTrip();
	testQuotedProfile();
	testUnquotedProfile();
	testWrongTagError();
	testEmptyParametersError();
	testTagWithoutPayloadError();
	testLowercaseStateRejected();
	testSerializeRoundTrips();
	harness.report();
}
