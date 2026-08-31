/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

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
};

vector<FilterFactoryRegistration>& registrations()
{
	static vector<FilterFactoryRegistration> registeredFactories;
	return registeredFactories;
}
}

bool FilterFactoryRegistry::registerFactory(int priority, FilterFactoryCreator creator,
	vector<wstring> commandKeywords)
{
	registrations().push_back({priority, creator, std::move(commandKeywords)});
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

wstring FilterFactoryRegistry::canonicalCommand(const wstring& key)
{
	const size_t first = key.find_first_not_of(L" \t");
	if (first == wstring::npos)
		return wstring();

	const size_t last = key.find_last_not_of(L" \t");
	const wstring trimmedKey = key.substr(first, last - first + 1);

	// The Filter family is the one place a trailing token is part of the
	// grammar. BiQuadFilterFactory and IIRFilterFactory both match with
	// rfind(L"Filter", 0) == 0, so "Filter", "Filter 1" and even "Filter1" all
	// reach them - IIRCommandTests pins the unspaced form. Reproducing the
	// prefix here rather than comparing the first token is what keeps this
	// function honest about what the engine will actually run.
	static const wstring filterPrefix = L"Filter";
	if (trimmedKey.starts_with(filterPrefix) && knownConfigCommands().count(filterPrefix) != 0)
		return filterPrefix;

	// Every other factory compares the whole key for equality, so a trailing
	// token means the line is not that command at all: the engine reads
	// "Channel 2:" as an unrecognized key and never runs it. Matching only the
	// first token here would tell callers otherwise, and the Editor would then
	// rewrite such a line into canonical spelling on the first edit - turning a
	// line the engine ignores into one it executes.
	if (knownConfigCommands().count(trimmedKey) == 0)
		return wstring();

	return trimmedKey;
}
