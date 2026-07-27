#pragma once

#include "engine/IFilterFactory.h"

class HilbertFilterFactory : public ParseReportingFactory
{
public:
	FilterVector createFilter(const std::wstring& configPath,
		std::wstring& command, std::wstring& parameters) override;
};
