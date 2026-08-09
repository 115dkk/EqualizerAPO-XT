#include "stdafx.h"

#include <string>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fftw3.h>

#include "dsp/FftwPlanningPolicy.h"

namespace
{
std::mutex& plannerMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::string& wisdomPath()
{
	static std::string path;
	return path;
}

std::unordered_set<int>& exportedLengths()
{
	static std::unordered_set<int> lengths;
	return lengths;
}

void initializeWisdom()
{
	static std::once_flag initialized;
	std::call_once(initialized, [] {
		char appData[MAX_PATH] = {};
		if (GetEnvironmentVariableA("LOCALAPPDATA", appData, MAX_PATH) == 0)
			return;

		const std::string directory = std::string(appData) + "\\EqualizerAPO";
		CreateDirectoryA(directory.c_str(), nullptr);
		wisdomPath() = directory + "\\fftw_wisdom.dat";
		if (GetFileAttributesA(wisdomPath().c_str()) != INVALID_FILE_ATTRIBUTES)
			fftw_import_wisdom_from_filename(wisdomPath().c_str());
	});
}
}

FftwPlanningPolicy::Session::Session()
	: plannerLock(plannerMutex())
{
	initializeWisdom();
}

// Deliberately requires a live Session: the Session owns the planner lock.
// cppcheck-suppress functionStatic
unsigned FftwPlanningPolicy::Session::flags() const
{
	// The first plan for a transform length measures and becomes reusable
	// wisdom. Imported wisdom makes the same flag fast on later processes.
	return FFTW_MEASURE | FFTW_PRESERVE_INPUT;
}

// Deliberately requires a live Session: FFTW wisdom mutation is serialized by
// the planner lock held for this object's lifetime.
// cppcheck-suppress functionStatic
bool FftwPlanningPolicy::Session::exportWisdomForLength(int transformLength)
{
	if (wisdomPath().empty())
		return false;
	if (!exportedLengths().insert(transformLength).second)
		return true;
	return fftw_export_wisdom_to_filename(wisdomPath().c_str()) != 0;
}
