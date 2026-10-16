/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "SubwooferRoutingFilterFactory.h"

#include <string>
#include <utility>

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/StateCodec.h"
#include "engine/ConfigurationFileReader.h"
#include "filters/ConfigFileReference.h"
#include "filters/FilterFactoryRegistry.h"
#include "SubwooferRoutingCommand.h"
#include "SubwooferRoutingFilter.h"

namespace
{
std::wstring codecErrorMessage(const subroute::StateDecodeResult& result)
{
	if (result.errors.empty())
		return L"state decoding failed";

	const subroute::StateCodecError& error = result.errors.front();
	std::string message = error.message;
	if (!error.jsonPointer.empty())
		message = error.jsonPointer + ": " + message;
	return subwooferRoutingFromUtf8(message);
}

std::wstring validationErrorMessage(
	const subroute::ValidationDiagnostic& diagnostic)
{
	std::string message = diagnostic.message;
	if (!diagnostic.entityId.empty())
		message = diagnostic.entityId + ": " + message;
	return subwooferRoutingFromUtf8(message);
}
}

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_FACTORY(FilterFactoryPriority::SubwooferRouting, SubwooferRoutingFilterFactory, L"SubwooferRouting")

LineOutcome SubwooferRoutingFilterFactory::createFilter(
	const std::wstring& configPath, std::wstring& command,
	std::wstring& parameters)
{
	if (command != L"SubwooferRouting")
		return {};

	SubwooferRoutingCommand parsed;
	std::wstring error;
	if (!SubwooferRoutingCommand::parse(command, parameters, parsed, &error))
		return LineOutcome::malformed(error);

	std::string utf8Text;
	if (parsed.form == SubwooferRoutingCommand::Form::Profile)
	{
		const ConfigFileReference::Target profile =
			ConfigFileReference::target(configPath, parsed.payload);
		if (!profile.refusal.empty())
			return LineOutcome::malformed(profile.refusal);
		if (profile.path.empty())
			return LineOutcome::malformed(
				L"expected the path of a profile file");
		const std::wstring& resolvedPath = profile.path.path();

		if (!ConfigurationFileReader::readBytes(profile.path, utf8Text))
		{
			return LineOutcome::malformed(
				L"could not read profile file \"" + resolvedPath + L"\"");
		}

		if (utf8Text.size() >= 3
			&& static_cast<unsigned char>(utf8Text[0]) == 0xef
			&& static_cast<unsigned char>(utf8Text[1]) == 0xbb
			&& static_cast<unsigned char>(utf8Text[2]) == 0xbf)
		{
			utf8Text.erase(0, 3);
		}
	}
	else
	{
		utf8Text = subwooferRoutingToUtf8(parsed.payload);
	}

	subroute::StateDecodeResult decoded = subroute::decodeState(utf8Text);
	if (!decoded.state.has_value())
		return LineOutcome::malformed(codecErrorMessage(decoded));

	subroute::SubwooferRoutingState state = std::move(*decoded.state);
	const subroute::ValidationResult validation = subroute::validate(state);
	const subroute::ValidationDiagnostic* diagnostic =
		firstSubwooferRoutingValidationError(validation);
	if (diagnostic != nullptr)
		return LineOutcome::malformed(validationErrorMessage(*diagnostic));

	return LineOutcome::produced(singleFilter(
		makeFilter<SubwooferRoutingFilter>(std::move(state))));
}
