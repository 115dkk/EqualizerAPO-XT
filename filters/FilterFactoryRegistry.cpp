#include "stdafx.h"
#include "FilterFactoryRegistry.h"

#include <algorithm>
#include <set>
#include <string>

using std::set;
using std::stable_sort;
using std::unique_ptr;
using std::vector;
using std::wstring;

namespace
{
struct FilterFactoryRegistration
{
	int priority = 0;
	FilterFactoryCreator creator = nullptr;
	vector<wstring> commandKeywords;
	bool suppressMissingFilterWarning = false;
};

vector<FilterFactoryRegistration>& registrations()
{
	static vector<FilterFactoryRegistration> registeredFactories;
	return registeredFactories;
}
}

bool FilterFactoryRegistry::registerFactory(int priority, FilterFactoryCreator creator,
	vector<wstring> commandKeywords, bool suppressMissingFilterWarning)
{
	registrations().push_back({priority, creator, std::move(commandKeywords), suppressMissingFilterWarning});
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

const set<wstring>& FilterFactoryRegistry::knownConfigCommands()
{
	// Union of every registered factory's command keyword(s). "Filter" is shared
	// by IIR and BiQuad (both match a key starting with "Filter", e.g. "Filter 1").
	// Derived once and cached; by first call every REGISTER_FILTER_FACTORY static
	// initializer has run, and /WHOLEARCHIVE pulls every factory TU into the link.
	static const set<wstring> commands = []() {
		set<wstring> result;
		for (const FilterFactoryRegistration& registration : registrations())
			for (const wstring& keyword : registration.commandKeywords)
				result.insert(keyword);
		return result;
	}();
	return commands;
}

const set<wstring>& FilterFactoryRegistry::commandsWithoutFilter()
{
	static const set<wstring> commands = []() {
		set<wstring> result;
		for (const FilterFactoryRegistration& registration : registrations())
			if (registration.suppressMissingFilterWarning)
				for (const wstring& keyword : registration.commandKeywords)
					result.insert(keyword);
		return result;
	}();
	return commands;
}
