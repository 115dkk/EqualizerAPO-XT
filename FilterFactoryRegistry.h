#pragma once

#include <memory>
#include <vector>

#include "IFilterFactory.h"

using FilterFactoryCreator = std::unique_ptr<IFilterFactory>(*)();

class FilterFactoryRegistry
{
public:
	static bool registerFactory(int priority, FilterFactoryCreator creator);
	static std::vector<std::unique_ptr<IFilterFactory>> createFactories();
};

#define REGISTER_FILTER_FACTORY(priority, factoryType) \
	namespace \
	{ \
		const bool factoryType##Registered = FilterFactoryRegistry::registerFactory(priority, []() -> std::unique_ptr<IFilterFactory> { return std::make_unique<factoryType>(); }); \
	}
