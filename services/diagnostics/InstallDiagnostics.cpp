/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See InstallDiagnostics.h. The section order follows the order a reader needs:
	what is installed, whether the audio engine can reach it, and what it is
	attached to. Every section keeps going when its own lookup fails, because a
	report that stops at the first missing key is a report that never covers the
	machine that actually has a problem.
*/

#include "services/diagnostics/InstallDiagnostics.h"

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "services/security/AudioEngineAccess.h"
#include "services/registry/IRegistry.h"
#include "services/registry/RegistryHelper.h"
#include "platform/windows/WindowsVersion.h"
#include "devices/DeviceAPOInfoKeys.h"

using std::vector;
using std::wstring;

namespace
{
// Reads a value and says so when it is not there, instead of throwing into the
// middle of a report. Every lookup here is about a machine that may be broken in
// exactly the way that makes the lookup fail.
wstring valueOrNote(const IRegistry& registry, const wstring& key, const wstring& valuename,
	const wchar_t* missingNote = L"(not set)")
{
	try
	{
		if (!registry.keyExists(key) || !registry.valueExists(key, valuename))
			return missingNote;
		return registry.readValue(key, valuename);
	}
	catch (const RegistryException&)
	{
		return L"(could not be read)";
	}
}

wstring dwordOrNote(const IRegistry& registry, const wstring& key, const wstring& valuename)
{
	try
	{
		if (!registry.keyExists(key) || !registry.valueExists(key, valuename))
			return L"(not set)";
		return std::to_wstring(registry.readDWORDValue(key, valuename));
	}
	catch (const RegistryException&)
	{
		return L"(could not be read)";
	}
}

bool pathExists(const wstring& path)
{
	return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

wstring environmentValue(const wchar_t* name)
{
	wchar_t buffer[MAX_PATH] = {};
	DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return wstring();
	return wstring(buffer, length);
}

bool startsWithNoCase(const wstring& text, const wstring& prefix)
{
	if (prefix.empty() || text.size() < prefix.size())
		return false;
	return CompareStringOrdinal(text.c_str(), static_cast<int>(prefix.size()),
		prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

void appendSection(vector<wstring>& lines, const wchar_t* title)
{
	lines.push_back(L"");
	lines.push_back(wstring(L"=== ") + title + L" ===");
}

// The install root and config directory, and what the two principals that matter
// are granted on them. This is the section the field failure lives in.
void appendAccessSection(vector<wstring>& lines, const wchar_t* title, const wstring& path,
	const wchar_t* whenEngineCannotRead)
{
	appendSection(lines, title);
	lines.push_back(L"path: " + (path.empty() ? wstring(L"(not set)") : path));
	if (!pathExists(path))
	{
		lines.push_back(L"WARNING: this path does not exist.");
		return;
	}

	const bool engineCanRead = AudioEngineAccess::isReadableByAudioEngine(path);
	const bool usersCanRun = AudioEngineAccess::isRunnableByUsers(path);
	lines.push_back(wstring(L"readable by audiodg (LOCAL SERVICE): ") + (engineCanRead ? L"yes" : L"NO"));
	lines.push_back(wstring(L"read+execute for BUILTIN\\Users:       ") + (usersCanRun ? L"yes" : L"no"));
	if (!engineCanRead)
		lines.push_back(wstring(L"WARNING: ") + whenEngineCannotRead);
	if (!usersCanRun)
		lines.push_back(L"WARNING: a user without administrator rights may not be able to run this.");
}

// One APO CLSID's registration: the DLL the COM server points at, and whether
// that file is still there. An install that was moved or partly removed shows up
// here as a registration pointing at nothing.
void appendComRegistration(vector<wstring>& lines, const IRegistry& registry,
	const wchar_t* label, const GUID& clsid)
{
	wstring guidString;
	try
	{
		guidString = RegistryHelper::getGuidString(clsid);
	}
	catch (const RegistryException&)
	{
		lines.push_back(wstring(label) + L": the CLSID could not be formatted");
		return;
	}

	const wstring inprocKey = wstring(clsidKeyPath) + L"\\" + guidString + L"\\InprocServer32";
	const wstring dll = valueOrNote(registry, inprocKey, L"", L"(not registered)");
	lines.push_back(wstring(label) + L" " + guidString + L" -> " + dll);
	if (dll.size() > 1 && dll[0] != L'(')
	{
		const bool exists = pathExists(dll);
		lines.push_back(wstring(L"  the DLL is on disk: ") + (exists ? L"yes" : L"NO"));
		if (!exists)
			lines.push_back(L"  WARNING: the COM registration points at a file that is not there. Reinstall.");
	}
}
} // namespace

namespace InstallDiagnostics
{

vector<wstring> attachedEndpoints(const IRegistry& registry)
{
	vector<wstring> lines;

	wstring preMix;
	wstring postMix;
	try
	{
		preMix = RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID);
		postMix = RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID);
	}
	catch (const RegistryException&)
	{
		lines.push_back(L"the APO CLSIDs could not be formatted, so no endpoint could be checked");
		return lines;
	}

	static const wchar_t* const slotNames[] = {L"LFX", L"GFX", L"SFX", L"MFX", L"EFX"};

	for (const wchar_t* root : {renderKeyPath, captureKeyPath})
	{
		vector<wstring> devices;
		try
		{
			if (!registry.keyExists(root))
				continue;
			devices = registry.enumSubKeys(root);
		}
		catch (const RegistryException&)
		{
			lines.push_back(wstring(L"could not enumerate ") + root);
			continue;
		}

		const bool input = wstring(root) == captureKeyPath;
		for (const wstring& deviceGuid : devices)
		{
			const wstring deviceKey = wstring(root) + L"\\" + deviceGuid;
			const wstring fxKey = deviceKey + L"\\FxProperties";

			// Not named "slots": Qt defines that as a macro, and this file is
			// compiled into DeviceSelector, where the name would vanish and the
			// declaration would not parse.
			wstring attachedSlots;
			try
			{
				if (!registry.keyExists(fxKey))
					continue;

				for (unsigned i = 0; i < allGuidValueNameCount; i++)
				{
					if (!registry.valueExists(fxKey, allGuidValueNames[i]))
						continue;
					const wstring held = registry.readValue(fxKey, allGuidValueNames[i]);
					const wchar_t* which = nullptr;
					if (CompareStringOrdinal(held.c_str(), -1, preMix.c_str(), -1, TRUE) == CSTR_EQUAL)
						which = L"pre-mix";
					else if (CompareStringOrdinal(held.c_str(), -1, postMix.c_str(), -1, TRUE) == CSTR_EQUAL)
						which = L"post-mix";
					if (which == nullptr)
						continue;

					if (!attachedSlots.empty())
						attachedSlots += L", ";
					attachedSlots += wstring(slotNames[i]) + L"=" + which;
				}
			}
			catch (const RegistryException&)
			{
				// A driver-locked endpoint key is exactly the kind of machine this
				// report is run on, so note it and keep going.
				lines.push_back(wstring(input ? L"capture " : L"render ") + deviceGuid
					+ L": its FxProperties could not be read");
				continue;
			}

			if (attachedSlots.empty())
				continue;

			const wstring propertiesKey = deviceKey + L"\\Properties";
			const wstring device = valueOrNote(registry, propertiesKey, deviceValueName, L"(unnamed)");
			const wstring connection = valueOrNote(registry, propertiesKey, connectionValueName, L"(unnamed)");
			lines.push_back(wstring(input ? L"capture: " : L"render:  ") + connection + L" " + device
				+ L" [" + attachedSlots + L"] " + deviceGuid);
		}
	}

	if (lines.empty())
		lines.push_back(L"no endpoint currently has Equalizer APO in its chain.");

	return lines;
}

vector<wstring> collect(const IRegistry& registry)
{
	vector<wstring> lines;

	appendSection(lines, L"Environment");
	SYSTEMTIME now;
	GetLocalTime(&now);
	wchar_t stamp[32];
	swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	lines.push_back(wstring(L"time:          ") + stamp);
	lines.push_back(wstring(L"elevated:      ") + (AudioEngineAccess::isElevated() ? L"yes" : L"no"));
	const WindowsVersion::Version version = WindowsVersion::current();
	lines.push_back(L"windows:       " + std::to_wstring(version.major) + L"."
		+ std::to_wstring(version.minor) + L"." + std::to_wstring(version.build));
	// The two version boundaries that change what this program does: 8.1 decides
	// which APO slots the driver can be asked for, and build 26100 is where
	// Windows started putting its own subkeys under FxProperties (issue #189),
	// which is why uninstall deletes values rather than the key.
	if (!WindowsVersion::isAtLeast(6, 3))
		lines.push_back(L"note: before Windows 8.1, only the LFX/GFX slots are available.");
	if (version.build >= 26100)
		lines.push_back(L"note: on this build Windows adds its own subkeys under FxProperties, so an "
			L"uninstall leaves the key in place and removes only the values it wrote.");

	appendSection(lines, L"EqualizerAPO registry");
	const wstring installPath = valueOrNote(registry, APP_REGPATH, L"InstallPath");
	const wstring configPath = valueOrNote(registry, APP_REGPATH, L"ConfigPath");
	lines.push_back(L"InstallPath:             " + installPath);
	lines.push_back(L"ConfigPath:              " + configPath);
	lines.push_back(L"EnableTrace:             " + valueOrNote(registry, APP_REGPATH, L"EnableTrace"));
	lines.push_back(L"DisableProtectedAudioDG: " + dwordOrNote(registry, protectedDGKeyPath, protectedDGValueName));
	if (dwordOrNote(registry, protectedDGKeyPath, protectedDGValueName) != L"1")
		lines.push_back(L"WARNING: without this set to 1 the audio engine runs protected and loads no APO of ours.");

	if (installPath.empty() || installPath[0] == L'(')
	{
		lines.push_back(L"");
		lines.push_back(L"Equalizer APO is not registered on this machine, so nothing below could be checked.");
		return lines;
	}

	appendSection(lines, L"APO COM registration");
	appendComRegistration(lines, registry, L"pre-mix ", EQUALIZERAPO_PRE_MIX_GUID);
	appendComRegistration(lines, registry, L"post-mix", EQUALIZERAPO_POST_MIX_GUID);

	appendSection(lines, L"Install location");
	const wstring profile = environmentValue(L"USERPROFILE");
	const wstring localAppData = environmentValue(L"LOCALAPPDATA");
	if (startsWithNoCase(installPath, profile) || startsWithNoCase(installPath, localAppData))
	{
		lines.push_back(L"the install lives under the installing user's profile, which is where Velopack puts it.");
		lines.push_back(L"audiodg has to be granted read+execute on that tree explicitly; the default ACL does not.");
	}
	else
	{
		lines.push_back(L"the install is outside the user's profile.");
	}

	appendAccessSection(lines, L"Install root access", installPath,
		L"audiodg cannot load EqualizerAPO.dll from here. This is what makes Device Selector "
		L"report GetMixFormat or Initialize as access denied.");

	if (!configPath.empty() && configPath[0] != L'(')
	{
		appendAccessSection(lines, L"Config directory access", configPath,
			L"audiodg cannot read config.txt, so the APO loads and then runs with no filters at all.");
	}

	appendSection(lines, L"Endpoints with Equalizer APO attached");
	for (const wstring& line : attachedEndpoints(registry))
		lines.push_back(line);

	appendSection(lines, L"What to do next");
	if (!pathExists(installPath))
	{
		lines.push_back(L"The install root is gone. Reinstall.");
	}
	else if (!AudioEngineAccess::isReadableByAudioEngine(installPath))
	{
		lines.push_back(L"Run Device Selector once from the Start menu and accept the elevation prompt;");
		lines.push_back(L"the install hook applies the missing grant. If that does not help, reinstall.");
	}
	else
	{
		lines.push_back(L"Nothing above points at a permission problem. If a device still produces no");
		lines.push_back(L"effect, set EnableTrace to true in HKLM\\SOFTWARE\\EqualizerAPO, reproduce it,");
		lines.push_back(L"and attach the log next to config.txt.");
	}

	return lines;
}

vector<wstring> collect()
{
	return collect(systemRegistry());
}

wstring writeReport()
{
	const vector<wstring> lines = collect();

	// The same directory LogHelper::useUserFile writes into, so a user asked for
	// "the logs folder" finds the report there too.
	wstring path;
	const wstring localAppData = environmentValue(L"LOCALAPPDATA");
	if (!localAppData.empty())
	{
		const wstring directory = localAppData + L"\\EqualizerAPO\\logs";
		if ((CreateDirectoryW((localAppData + L"\\EqualizerAPO").c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
			&& (CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS))
		{
			SYSTEMTIME now;
			GetLocalTime(&now);
			wchar_t stamp[32];
			swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d",
				now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
			const wstring candidate = directory + L"\\diagnose-" + stamp + L".txt";

			FILE* file = nullptr;
			if (_wfopen_s(&file, candidate.c_str(), L"wt, ccs=UTF-8") == 0 && file != nullptr)
			{
				for (const wstring& line : lines)
					fwprintf(file, L"%ls\n", line.c_str());
				fclose(file);
				path = candidate;
			}
		}
	}

	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		// Reopening the CRT's stdout onto the freshly attached console is what
		// makes this reach it; the handle the process started with goes nowhere.
		FILE* consoleOut = nullptr;
		if (freopen_s(&consoleOut, "CONOUT$", "w", stdout) == 0)
		{
			for (const wstring& line : lines)
				wprintf(L"%ls\n", line.c_str());
			if (!path.empty())
				wprintf(L"\nwritten to %ls\n", path.c_str());
			fflush(stdout);
		}
		FreeConsole();
	}

	return path;
}

} // namespace InstallDiagnostics
