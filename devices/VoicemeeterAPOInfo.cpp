/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2017  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "text/WideString.h"
#include "services/registry/RegistryPaths.h"
#include <ShlObj.h>
#include <fstream>
#include <algorithm>
#include <KsMedia.h>
#include <shellapi.h>
#include "platform/windows/ComPtr.h"
#include "platform/windows/ProcessCommandLine.h"
#include "platform/windows/ShellLink.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"
#include "platform/windows/Win32Resource.h"
#include "VoicemeeterAPOInfo.h"

using std::exception;
using std::find;
using std::list;
using std::make_shared;
using std::shared_ptr;
using std::sort;
using std::vector;
using std::wstringstream;
using std::wstring;
using winutil::ComPtr;
using winutil::CoTaskMem;

#include "VoicemeeterDetection.h"
static const wchar_t* startupFilename = L"Equalizer APO Voicemeeter Client.lnk";
static const wchar_t* clientFilename = L"VoicemeeterClient.exe";
static const wchar_t* voicemeeterClientKeyPath = USER_REGPATH L"\\Voicemeeter Client";
static const wchar_t* sampleRateValueName = L"sampleRate";

void VoicemeeterAPOInfo::prependInfos(vector<shared_ptr<AbstractAPOInfo>>& list, IRegistry& registry)
{
	wstring voicemeeterDirectory;
	if (registry.keyExists(voicemeeterKeyPath))
		voicemeeterDirectory = registry.readValue(voicemeeterKeyPath, uninstallStringValueName);
	else if (registry.keyExists(voicemeeterWowKeyPath))
		voicemeeterDirectory = registry.readValue(voicemeeterWowKeyPath, uninstallStringValueName);

	if (voicemeeterDirectory.length() > 0)
	{
		size_t index = voicemeeterDirectory.find_last_of(L'\\');

		wstring setupFilename = text::toLower(voicemeeterDirectory.substr(index + 1));
		long voicemeeterType = 1;
		if (setupFilename == L"voicemeeterprosetup.exe")
			voicemeeterType = 2;// banana
		else if (setupFilename == L"voicemeeter8setup.exe")
			voicemeeterType = 3;// potato

		unsigned outputCount;
		if (voicemeeterType == 3)
			outputCount = 5;
		else if (voicemeeterType == 2)
			outputCount = 3;
		else
			outputCount = 1;

		bool defaultDevice = false;
		list.erase(remove_if(list.begin(), list.end(), [&defaultDevice](const shared_ptr<AbstractAPOInfo>& info) {
			if (info->getDeviceName().find(L"VB-Audio VoiceMeeter") != wstring::npos)
			{
				if (info->isDefaultDevice())
					defaultDevice = true;

				return true;
			}

			return false;
			}), list.end());

		bool anyInstalled = false;
		for (unsigned i = 0; i < outputCount; i++)
		{
			wstringstream sstream;
			sstream << "Output A" << (i + 1);
			shared_ptr<AbstractAPOInfo> info = *list.insert(list.begin() + i, make_shared<VoicemeeterAPOInfo>(sstream.str(), true, registry));
			if (info->isInstalled())
				anyInstalled = true;
		}

		if (defaultDevice)
		{
			for (unsigned i = 0; i < outputCount; i++)
			{
				const shared_ptr<VoicemeeterAPOInfo>& info = (const shared_ptr<VoicemeeterAPOInfo>&)list[i];
				if (!anyInstalled || info->isInstalled())
				{
					info->defaultDevice = true;
					break;
				}
			}
		}
	}
	else
	{
		// Voicemeeter was uninstalled but Voicemeeter Client might still be installed
		wstring startupFilePath = getStartupPath();
		wstring argString = getLinkArgs(startupFilePath);
		vector<wstring> args = splitArgs(argString);

		int i = 0;
		for (wstring arg : args)
		{
			list.insert(list.begin() + i, make_shared<VoicemeeterAPOInfo>(arg, false, registry));
			i++;
		}
	}
}

VoicemeeterAPOInfo::VoicemeeterAPOInfo(const wstring& connectionName, bool voicemeeterInstalled, IRegistry& registry)
	: connectionName(connectionName), voicemeeterInstalled(voicemeeterInstalled), registry(registry)
{
	wstring startupFilePath = getStartupPath();
	wstring path;
	wstring argString = getLinkArgs(startupFilePath, &path);
	vector<wstring> args = splitArgs(argString);
	installed = find(args.begin(), args.end(), connectionName) != args.end();

	sampleRate = 48000;
	try
	{
		if (registry.keyExists(voicemeeterClientKeyPath) && registry.valueExists(voicemeeterClientKeyPath, sampleRateValueName))
			sampleRate = (unsigned)registry.readDWORDValue(voicemeeterClientKeyPath, sampleRateValueName);
	}
	catch (const RegistryError&)
	{
		// ignore
	}

	wstring clientPath = getClientPath();
	changes = (path != clientPath);
}

wstring VoicemeeterAPOInfo::getConnectionName() const
{
	return connectionName;
}

wstring VoicemeeterAPOInfo::getDeviceName() const
{
	return L"Voicemeeter";
}

wstring VoicemeeterAPOInfo::getDeviceGuid() const
{
	return L"";
}

wstring VoicemeeterAPOInfo::getDeviceString() const
{
	return getConnectionName() + L" " + getDeviceName();
}

unsigned VoicemeeterAPOInfo::getChannelCount() const
{
	return 8;
}

unsigned VoicemeeterAPOInfo::getSampleRate() const
{
	return sampleRate;
}

unsigned long VoicemeeterAPOInfo::getChannelMask() const
{
	return KSAUDIO_SPEAKER_7POINT1_SURROUND;
}

bool VoicemeeterAPOInfo::isInput() const
{
	return false;
}

bool VoicemeeterAPOInfo::isInstalled() const
{
	return installed;
}

bool VoicemeeterAPOInfo::canBeUpgraded() const
{
	return false;
}

bool VoicemeeterAPOInfo::hasChanges() const
{
	return changes;
}

bool VoicemeeterAPOInfo::isEnhancementsDisabled() const
{
	return false;
}

bool VoicemeeterAPOInfo::isDefaultDevice() const
{
	return defaultDevice;
}

bool VoicemeeterAPOInfo::isDisabled() const
{
	return false;
}

bool VoicemeeterAPOInfo::isUnplugged() const
{
	return false;
}

bool VoicemeeterAPOInfo::isVoicemeeterInstalled() const
{
	return voicemeeterInstalled;
}

void VoicemeeterAPOInfo::install()
{
	wstring startupFilePath = getStartupPath();
	wstring argString = getLinkArgs(startupFilePath);
	vector<wstring> args = splitArgs(argString);
	vector<wstring>::iterator it = find(args.begin(), args.end(), connectionName);
	if (it == args.end())
		args.push_back(connectionName);
	sort(args.begin(), args.end());
	argString = joinArgs(args);

	wstring clientPath = getClientPath();

	createLink(startupFilePath, clientPath, argString);
}

void VoicemeeterAPOInfo::uninstall()
{
	wstring startupFilePath = getStartupPath();
	wstring argString = getLinkArgs(startupFilePath);
	vector<wstring> args = splitArgs(argString);
	vector<wstring>::iterator it = find(args.begin(), args.end(), connectionName);
	if (it != args.end())
		args.erase(it);
	if (args.size() > 0)
	{
		sort(args.begin(), args.end());
		argString = joinArgs(args);

		createLink(startupFilePath, getClientPath(), argString);
	}
	else
	{
		DeleteFileW(startupFilePath.c_str());
	}
}

void VoicemeeterAPOInfo::reinstall()
{
	uninstall();
	install();
}

wstring VoicemeeterAPOInfo::getStartupPath()
{
	CoTaskMem<wchar_t> startupPath;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DONT_UNEXPAND,
		nullptr, startupPath.put())))
	{
		return wstring();
	}
	wstring result(startupPath.get());

	return result + L"\\" + startupFilename;
}

wstring VoicemeeterAPOInfo::getClientPath()
{
	wchar_t filename[MAX_PATH];
	GetModuleFileNameW(nullptr, filename, ARRAYSIZE(filename));
	PathRemoveFileSpecW(filename);
	wstring clientPath = filename;
	clientPath = clientPath + L"\\" + clientFilename;

	return clientPath;
}

void VoicemeeterAPOInfo::createLink(const wstring& lnkPath, const wstring& path, const wstring& args)
{
	// Audit #250 F035: every HRESULT here used to be discarded, so a failed
	// startup link (the thing that keeps the client running after reboot)
	// left no trace at all. Still best-effort, but now it says so. The
	// IShellLink choreography itself is winutil::writeShellLink (audit #275 C5).
	const HRESULT hr = winutil::writeShellLink(
		path, std::wstring(), args, std::wstring(), std::wstring(), 0, lnkPath);
	if (FAILED(hr))
		LogFStatic(L"Could not create link %s (HRESULT 0x%08X)", lnkPath.c_str(), hr);
}

wstring VoicemeeterAPOInfo::getLinkArgs(const wstring& lnkPath, wstring* path)
{
	wstring result;

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(shellLink.put()));
	if (SUCCEEDED(hr))
	{
		ComPtr<IPersistFile> persistFile;
		hr = shellLink->QueryInterface(IID_PPV_ARGS(persistFile.put()));
		if (SUCCEEDED(hr))
		{
			hr = persistFile->Load(lnkPath.c_str(), STGM_READ);
			if (SUCCEEDED(hr))
			{
				wchar_t buf[MAX_PATH];
				hr = shellLink->GetArguments(buf, ARRAYSIZE(buf));
				if (SUCCEEDED(hr))
					result = buf;

				if (path != nullptr)
				{
					hr = shellLink->GetPath(buf, ARRAYSIZE(buf), nullptr, 0);
					if (SUCCEEDED(hr))
						*path = buf;
				}
			}
		}
	}

	return result;
}

vector<wstring> VoicemeeterAPOInfo::splitArgs(const wstring& argString)
{
	vector<wstring> result;

	if (argString.length() > 0)
	{
		int argc;
		winutil::UniqueLocalPtr<wchar_t*> argv(CommandLineToArgvW(argString.c_str(), &argc));
		if (!argv)
			return result;
		for (int i = 0; i < argc; i++)
			result.push_back(argv.get()[i]);
	}

	return result;
}

wstring VoicemeeterAPOInfo::joinArgs(const vector<wstring>& args)
{
	wstring result;
	for (const wstring& arg : args)
	{
		if (result.length() > 0)
			result += L" ";

		if (arg.find(' ') != wstring::npos)
			result += L"\"" + arg + L"\"";
		else
			result += arg;
	}

	return result;
}

void VoicemeeterAPOInfo::ensureVoicemeeterClientRunning()
{
	wstring startupFilePath = getStartupPath();
	wstring argString = getLinkArgs(startupFilePath);
	vector<wstring> args = splitArgs(argString);
	wstring clientPath = getClientPath();

	// The privilege dance and the PEB walk live in
	// platform/windows/ProcessCommandLine.cpp (audit #275 C5); what stays
	// here is the Voicemeeter decision: a client whose command line does not
	// match the startup link's is asked to close and relaunched.
	bool matchingProcessExists = false;
	for (const winutil::ProcessWithCommandLine& process
		: winutil::findProcessesByExeName(clientFilename))
	{
		vector<wstring> processArgs = splitArgs(process.commandLine);
		wstring path = processArgs.front();
		processArgs.erase(processArgs.begin());

		if (path != clientPath || processArgs != args)
			winutil::requestProcessClose(process.processId);
		else
			matchingProcessExists = true;
	}

	if (!matchingProcessExists && !args.empty())
	{
		ShellExecuteW(nullptr, nullptr, clientPath.c_str(), argString.c_str(), nullptr, SW_SHOWDEFAULT);
	}
}

void VoicemeeterAPOInfo::saveVoicemeeterSampleRate(unsigned sampleRate, IRegistry& registry)
{
	try
	{
		registry.createKey(voicemeeterClientKeyPath);
		registry.writeDWORDValue(voicemeeterClientKeyPath, sampleRateValueName, sampleRate);
	}
	catch (const RegistryError&)
	{
		// ignore
	}
}

