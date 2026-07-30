/*
	This file is part of EqualizerAPO-XT.

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
#include "BassManagementFilterFactory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "BassManagement/Compiler.h"
#include "BassManagement/StateCodec.h"
#include "filters/ConvolutionFilePath.h"
#include "filters/FilterFactoryRegistry.h"
#include "helpers/StringHelper.h"
#include "BassManagementCommand.h"
#include "BassManagementFilter.h"

namespace
{
constexpr unsigned Utf8CodePage = 65001;

std::wstring fromUtf8(const std::string& text)
{
	return StringHelper::toWString(text, Utf8CodePage);
}

std::wstring codecErrorMessage(const bassmgmt::StateDecodeResult& result)
{
	if (result.errors.empty())
		return L"state decoding failed";

	const bassmgmt::StateCodecError& error = result.errors.front();
	std::string message = error.message;
	if (!error.jsonPointer.empty())
		message = error.jsonPointer + ": " + message;
	return fromUtf8(message);
}

const bassmgmt::ValidationDiagnostic* firstValidationError(
	const bassmgmt::ValidationResult& result)
{
	for (const bassmgmt::ValidationDiagnostic& diagnostic : result.diagnostics)
	{
		if (diagnostic.severity == bassmgmt::DiagnosticSeverity::Error)
			return &diagnostic;
	}
	return nullptr;
}

std::wstring validationErrorMessage(
	const bassmgmt::ValidationDiagnostic& diagnostic)
{
	std::string message = diagnostic.message;
	if (!diagnostic.entityId.empty())
		message = diagnostic.entityId + ": " + message;
	return fromUtf8(message);
}

bool readProfile(const std::wstring& path, std::string& text)
{
	std::ifstream stream(std::filesystem::path(path), std::ios::binary);
	if (!stream.is_open())
		return false;

	text.assign(std::istreambuf_iterator<char>(stream),
		std::istreambuf_iterator<char>());
	return !stream.bad();
}
}

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_FACTORY(FilterFactoryPriority::BassManagement, BassManagementFilterFactory, L"BassManagement")

FilterVector BassManagementFilterFactory::createFilter(
	const std::wstring& configPath, std::wstring& command,
	std::wstring& parameters)
{
	if (command != L"BassManagement")
		return {};

	BassManagementCommand parsed;
	std::wstring error;
	if (!BassManagementCommand::parse(command, parameters, parsed, &error))
		return reportParseError(command, error);

	std::string utf8Text;
	if (parsed.form == BassManagementCommand::Form::Profile)
	{
		const std::wstring resolvedPath =
			ConvolutionFilePath::resolve(configPath, parsed.payload);
		if (resolvedPath.empty())
			return reportParseError(command,
				L"expected the path of a profile file");

		if (!readProfile(resolvedPath, utf8Text))
		{
			return reportParseError(command,
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
		utf8Text = bassManagementToUtf8(parsed.payload);
	}

	bassmgmt::StateDecodeResult decoded = bassmgmt::decodeState(utf8Text);
	if (!decoded.state.has_value())
		return reportParseError(command, codecErrorMessage(decoded));

	bassmgmt::BassManagementState state = std::move(*decoded.state);
	const bassmgmt::ValidationResult validation = bassmgmt::validate(state);
	const bassmgmt::ValidationDiagnostic* diagnostic =
		firstValidationError(validation);
	if (diagnostic != nullptr)
		return reportParseError(command, validationErrorMessage(*diagnostic));

	return singleFilter(
		makeFilter<BassManagementFilter>(std::move(state)));
}
