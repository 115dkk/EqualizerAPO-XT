#include "stdafx.h"
#include "FilterFactoryRegistry.h"

#include <algorithm>

using std::stable_sort;
using std::unique_ptr;
using std::vector;

namespace
{
struct FilterFactoryRegistration
{
	int priority;
	FilterFactoryCreator creator;
};

vector<FilterFactoryRegistration>& registrations()
{
	static vector<FilterFactoryRegistration> registeredFactories;
	return registeredFactories;
}
}

bool FilterFactoryRegistry::registerFactory(int priority, FilterFactoryCreator creator)
{
	registrations().push_back({priority, creator});
	return true;
}

vector<unique_ptr<IFilterFactory>> FilterFactoryRegistry::createFactories()
{
	vector<FilterFactoryRegistration> sortedRegistrations = registrations();
	stable_sort(sortedRegistrations.begin(), sortedRegistrations.end(), [](const FilterFactoryRegistration& left, const FilterFactoryRegistration& right) {
		return left.priority < right.priority;
	});

	vector<unique_ptr<IFilterFactory>> factories;
	factories.reserve(sortedRegistrations.size());
	for (const FilterFactoryRegistration& registration : sortedRegistrations)
		factories.push_back(registration.creator());

	return factories;
}
