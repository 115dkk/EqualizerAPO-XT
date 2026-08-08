/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StartMenuShortcuts.h"

#include <cstdarg>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <objbase.h>
#include <objidl.h>

#include "ComPtr.h"
#include "LogHelper.h"
#include "PathHelper.h"
#include "Win32Resource.h"

namespace
{
using pathutil::joinPath;
using pathutil::fileExists;
using pathutil::createDirectoryRecursive;

constexpr wchar_t kShortcutFolderName[] = L"EqualizerAPO-XT";
constexpr wchar_t kDeviceSelectorShortcutFile[] = L"Device Selector.lnk";

void logLine(const wchar_t* level, const wchar_t* format, ...)
{
	wchar_t buffer[1024];
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(buffer, _TRUNCATE, format, args);
	va_end(args);
	LogFStatic(L"[StartMenuShortcuts] %s: %s", level, buffer);
}

std::wstring publicProgramsPath()
{
	winutil::UniqueCoTaskMemPtr<wchar_t> raw;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, raw.put());
	if (FAILED(hr) || !raw)
		return std::wstring();
	std::wstring path(raw.get());
	return path;
}

HRESULT writeShellLink(const std::wstring& target, const std::wstring& workingDir,
	const std::wstring& description, const std::wstring& iconPath, int iconIndex,
	const std::wstring& linkPath)
{
	winutil::ComPtr<IShellLinkW> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
		IID_IShellLinkW, reinterpret_cast<void**>(shellLink.put()));
	if (FAILED(hr) || !shellLink)
		return hr;

	shellLink->SetPath(target.c_str());
	if (!workingDir.empty())
		shellLink->SetWorkingDirectory(workingDir.c_str());
	if (!description.empty())
		shellLink->SetDescription(description.c_str());
	if (!iconPath.empty())
		shellLink->SetIconLocation(iconPath.c_str(), iconIndex);

	winutil::ComPtr<IPersistFile> persistFile;
	hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persistFile.put()));
	if (SUCCEEDED(hr) && persistFile)
	{
		hr = persistFile->Save(linkPath.c_str(), TRUE);
	}
	return hr;
}
}

namespace StartMenuShortcuts
{
bool create(const std::wstring& installDir)
{
	std::wstring deviceSelector = joinPath(installDir, L"DeviceSelector.exe");
	if (!fileExists(deviceSelector))
	{
		logLine(L"WARN", L"DeviceSelector.exe not found at %s", deviceSelector.c_str());
		return false;
	}

	std::wstring programsDir = publicProgramsPath();
	if (programsDir.empty())
	{
		logLine(L"ERR", L"SHGetKnownFolderPath(FOLDERID_CommonPrograms) failed");
		return false;
	}

	std::wstring shortcutFolder = joinPath(programsDir, kShortcutFolderName);
	if (!createDirectoryRecursive(shortcutFolder))
	{
		logLine(L"ERR", L"Failed to create %s", shortcutFolder.c_str());
		return false;
	}

	winutil::ComApartment apartment(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (!apartment.isUsable())
	{
		logLine(L"ERR", L"COM initialization failed (hr=0x%08lx)",
			static_cast<unsigned long>(apartment.status()));
		return false;
	}

	std::wstring linkPath = joinPath(shortcutFolder, kDeviceSelectorShortcutFile);
	HRESULT hr = writeShellLink(deviceSelector, installDir,
		L"Configure which audio devices use EqualizerAPO",
		deviceSelector, 0, linkPath);

	if (FAILED(hr))
	{
		logLine(L"ERR", L"Failed to write %s (hr=0x%08lx)", linkPath.c_str(), static_cast<unsigned long>(hr));
		return false;
	}
	logLine(L"INFO", L"Wrote shortcut %s", linkPath.c_str());
	return true;
}

bool remove()
{
	std::wstring programsDir = publicProgramsPath();
	if (programsDir.empty())
		return false;

	std::wstring shortcutFolder = joinPath(programsDir, kShortcutFolderName);
	std::wstring linkPath = joinPath(shortcutFolder, kDeviceSelectorShortcutFile);

	bool ok = true;
	if (fileExists(linkPath) && !DeleteFileW(linkPath.c_str()))
	{
		logLine(L"WARN", L"DeleteFile failed for %s (gle=%lu)", linkPath.c_str(), GetLastError());
		ok = false;
	}

	// Best-effort cleanup of empty folder; ignore failure (other lnks may live there).
	RemoveDirectoryW(shortcutFolder.c_str());
	return ok;
}
}
