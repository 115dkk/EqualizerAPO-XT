#pragma once

#include "engine/IFilterFactory.h"

class VelvetFilterFactory : public ParseReportingFactory
{
public:
	void initialize(FilterEngine* engine) override;
	FilterVector createFilter(const std::wstring& configPath,
		std::wstring& command, std::wstring& parameters) override;

private:
	FilterEngine* engine = nullptr;
};
