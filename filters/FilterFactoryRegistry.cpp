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

const set<wstring>& FilterFactoryRegistry::knownConfigCommands()
{
	// Derived from the command keywords matched by the registered factories.
	// "Filter" is the canonical keyword for both IIR and BiQuad filters, which
	// match any key starting with "Filter" (e.g. "Filter 1"). Keep this list in
	// sync with the factories' createFilter() command checks.
	static const set<wstring> commands = {
		L"Device",            // DeviceFilterFactory
		L"If", L"ElseIf", L"Else", L"EndIf", // IfFilterFactory
		L"Eval",              // ExpressionFilterFactory
		L"Include",           // IncludeFilterFactory
		L"Stage",             // StageFilterFactory
		L"Channel",           // ChannelFilterFactory
		L"Filter",            // IIRFilterFactory / BiQuadFilterFactory
		L"Preamp",            // PreampFilterFactory
		L"Delay",             // DelayFilterFactory
		L"Copy",              // CopyFilterFactory
		L"Convolution",       // ConvolutionFilterFactory
		L"GraphicEQ",         // GraphicEQFilterFactory
		L"VSTPlugin",         // VSTPluginFilterFactory
		L"LoudnessCorrection" // LoudnessCorrectionFilterFactory
	};
	return commands;
}
