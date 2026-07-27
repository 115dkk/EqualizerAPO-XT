#include "stdafx.h"
#include "HilbertFilterFactory.h"

#include "filters/FilterFactoryRegistry.h"
#include "HilbertCommand.h"
#include "HilbertFilter.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_FACTORY(FilterFactoryPriority::Hilbert,
	HilbertFilterFactory, L"Hilbert")

FilterVector HilbertFilterFactory::createFilter(const std::wstring& configPath,
	std::wstring& command, std::wstring& parameters)
{
	(void)configPath;
	if (command != L"Hilbert")
		return {};
	HilbertCommand parsed;
	std::wstring error;
	if (!HilbertCommand::parse(command, parameters, parsed, &error))
		return reportParseError(command, error);
	return singleFilter(makeFilter<HilbertFilter>(parsed));
}
