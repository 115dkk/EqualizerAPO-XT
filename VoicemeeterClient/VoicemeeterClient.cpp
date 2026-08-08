/*
	This file is part of Equalizer APO, a system-wide equalizer.
	Copyright (C) 2022  Jonas Thedering

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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "../helpers/RegistryHelper.h"
#include "VoicemeeterClient.h"
#include "../devices/VoicemeeterAPOInfo.h"

#include "../devices/VoicemeeterDetection.h"
#ifdef _WIN64
#define voicemeeterRemoteFileName L"VoicemeeterRemote64.dll"
#else
#define voicemeeterRemoteFileName L"VoicemeeterRemote.dll"
#endif
#define IDM_RESTART 200

using std::find;
using std::min;
using std::vector;
using std::wstringstream;
using std::wstring;

static long __stdcall callback(void* lpUser, long nCommand, void* lpData, long nnn);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd)
{
	vector<wstring> outputs;
	if (lpCmdLine[0] != 0)
	{
		int argc;
		winutil::UniqueLocalPtr<wchar_t*> argv(CommandLineToArgvW(lpCmdLine, &argc));
		if (!argv)
			return -1;
		for (int i = 0; i < argc; i++)
			outputs.push_back(argv.get()[i]);
	}

	try
	{
		VoicemeeterClient client(outputs);
		client.run();
		return 0;
	}
	catch (const InitError& e)
	{
		MessageBoxW(nullptr, e.getMessage().c_str(), L"Equalizer APO Voicemeeter Client Initialization Error", MB_APPLMODAL | MB_OK | MB_ICONERROR);
		return -1;
	}
	catch (const RegistryException& e)
	{
		MessageBoxW(nullptr, e.getMessage().c_str(), L"Equalizer APO Voicemeeter Client Initialization Error", MB_APPLMODAL | MB_OK | MB_ICONERROR);
		return -1;
	}
}

VoicemeeterClient::VoicemeeterClient(const vector<wstring>& outputs)
	: outputs(outputs)
{
	mainThreadId = GetCurrentThreadId();

	wstring voicemeeterDirectory;
	if (RegistryHelper::keyExists(voicemeeterKeyPath))
		voicemeeterDirectory = RegistryHelper::readValue(voicemeeterKeyPath, uninstallStringValueName);
	else if (RegistryHelper::keyExists(voicemeeterWowKeyPath))
		voicemeeterDirectory = RegistryHelper::readValue(voicemeeterWowKeyPath, uninstallStringValueName);

	size_t index = voicemeeterDirectory.find_last_of(L'\\');
	if (index != wstring::npos)
		voicemeeterDirectory.resize(index);
	else
		throw InitError(L"Voicemeeter is not installed");

	module.reset(LoadLibraryW((voicemeeterDirectory + L"\\" voicemeeterRemoteFileName).c_str()));
	if (!module)
		throw InitError(L"Failed to load " voicemeeterRemoteFileName);

	vmr = {};

#define LOAD_PROC(proc) vmr.proc = (T_ ## proc)GetProcAddress(module.get(), # proc);if (vmr.proc == nullptr) throw InitError(L"Did not find function \"" # proc L"\" in " voicemeeterRemoteFileName)
	LOAD_PROC(VBVMR_Login);
	LOAD_PROC(VBVMR_Logout);
	LOAD_PROC(VBVMR_GetVoicemeeterType);
	LOAD_PROC(VBVMR_IsParametersDirty);
	LOAD_PROC(VBVMR_AudioCallbackRegister);
	LOAD_PROC(VBVMR_AudioCallbackStart);
	LOAD_PROC(VBVMR_AudioCallbackStop);
	LOAD_PROC(VBVMR_AudioCallbackUnregister);

	try
	{
		initSoftware();
	}
	catch (...)
	{
		endSoftware();
		throw;
	}
}

VoicemeeterClient::~VoicemeeterClient()
{
	if (wTimer != 0)
	{
		KillTimer(nullptr, wTimer);
		wTimer = 0;
	}
	endSoftware();
}

void VoicemeeterClient::run()
{
	wTimer = SetTimer(nullptr, 0, 500, nullptr);
	PostThreadMessage(mainThreadId, WM_COMMAND, IDM_RESTART, 0);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		switch (msg.message)
		{
		case WM_COMMAND:
			handleCommand(msg.wParam, msg.lParam);
			break;
		case WM_TIMER:
			if (msg.wParam == wTimer)
			{
				// check if Voicemeeter type has changed
				if (vmr.VBVMR_IsParametersDirty() >= 0)
				{
					if (!connected.load())
						detectVoicemeeterType();
				}
				else
				{
					// Voicemeeter has been shut down
					connected.store(false);
				}
			}
			break;
		}
	}
}

void VoicemeeterClient::handle(long nCommand, void* lpData, long nnn)
{
	switch (nCommand)
	{
	case VBVMR_CBCOMMAND_STARTING:
	{
		VBVMR_LPT_AUDIOINFO audioInfo = (VBVMR_LPT_AUDIOINFO)lpData;
		const float newSampleRate = static_cast<float>(audioInfo->samplerate);
		const unsigned newMaxFrameCount = audioInfo->nbSamplePerFrame;
		sampleRate.store(newSampleRate);
		maxFrameCount.store(newMaxFrameCount);
		engineState.withLock([&](const EngineState& state) {
			for (const auto& engine : state.engines)
				if (engine != nullptr)
					engine->initialize(newSampleRate, 8, 8, 8, 0, newMaxFrameCount);
		});
		VoicemeeterAPOInfo::saveVoicemeeterSampleRate((unsigned)audioInfo->samplerate);
	}
	break;
	case VBVMR_CBCOMMAND_ENDING:
		break;
	case VBVMR_CBCOMMAND_CHANGE:
		PostThreadMessage(mainThreadId, WM_COMMAND, IDM_RESTART, 0);
		break;
	case VBVMR_CBCOMMAND_BUFFER_OUT:
	{
		VBVMR_LPT_AUDIOBUFFER audioBuffer = (VBVMR_LPT_AUDIOBUFFER)lpData;
		int nbi = audioBuffer->audiobuffer_nbi;
		int nbo = audioBuffer->audiobuffer_nbo;
		unsigned n = min(nbi, nbo) / 8;

		engineState.withLock([&](EngineState& state) {
			for (unsigned i = 0; i < n; i++)
			{
				FilterEngine* engine = nullptr;
				if (i < state.engines.size())
					engine = state.engines[i].get();

				bool idle = true;
				bool inputSilent = true;
				if (engine != nullptr)
				{
					inputSilent = isBufferSilent(audioBuffer->audiobuffer_r + 8 * i, audioBuffer->audiobuffer_nbs);
					idle = inputSilent && state.idleSampleCounts[i] > 10 * engine->getSampleRate();
				}

				// avoid processing when idle (Voicemeeter does still call this when no audio is played)
				if (!idle)
				{
					engine->process(audioBuffer->audiobuffer_w + 8 * i, audioBuffer->audiobuffer_r + 8 * i, audioBuffer->audiobuffer_nbs);

					bool outputSilent = isBufferSilent(audioBuffer->audiobuffer_w + 8 * i, audioBuffer->audiobuffer_nbs);
					if (inputSilent && outputSilent)
						state.idleSampleCounts[i] += audioBuffer->audiobuffer_nbs;
					else
						state.idleSampleCounts[i] = 0;
				}
				else
				{
					for (int j = 0; j < 8; j++)
						std::copy_n(audioBuffer->audiobuffer_r[8 * i + j], audioBuffer->audiobuffer_nbs, audioBuffer->audiobuffer_w[8 * i + j]);
				}
			}
		});
	}
	break;
	}
}

void VoicemeeterClient::initSoftware()
{
	long rep = vmr.VBVMR_Login();
	if (rep < 0)
		throw InitError(L"Failed To Login");
	loggedIn = true;
	if (vmr.VBVMR_IsParametersDirty() == 0)
		detectVoicemeeterType();
	else
		connected.store(false);
	unsigned tries = 30;
	bool loop = true;
	while (loop)
	{
		loop = false;
		char clientName[64] = "Equalizer APO";
		rep = vmr.VBVMR_AudioCallbackRegister(VBVMR_AUDIOCALLBACK_OUT, callback, this, clientName);
		if (rep == 1)
		{
			// Audit #250 F046: the third-party DLL fills clientName and does
			// not promise termination; force it and bound the format read.
			clientName[sizeof(clientName) - 1] = '\0';
			wchar_t message[512];
			_snwprintf_s(message, _TRUNCATE,
				L"Voicemeeter Output Insert already in use by:\n%.63S", clientName);
			throw InitError(message);
		}
		else if (rep != 0)
		{
			if (tries > 1)
			{
				// sometimes fails temporarily after restarting VoicemeeterClient
				Sleep(100);
				tries--;
				loop = true;
			}
			else
			{
				throw InitError(L"Failed to register audio callback");
			}
		}
		else
		{
			callbackRegistered = true;
		}
	}
}

void VoicemeeterClient::detectVoicemeeterType()
{
	long vmType;
	long rep = vmr.VBVMR_GetVoicemeeterType(&vmType);
	if (rep == 0)
	{
		connected.store(true);

		unsigned outputCount;
		if (vmType == 3)
			outputCount = 5;
		else if (vmType == 2)
			outputCount = 3;
		else
			outputCount = 1;

		bool sizeChanged = engineState.withLock([&](const EngineState& state) {
			return outputCount != state.engines.size();
		});
		if (sizeChanged)
		{
			EngineState replacement;
			replacement.engines.reserve(outputCount);
			replacement.idleSampleCounts.reserve(outputCount);

			for (unsigned i = 0; i < outputCount; i++)
			{
				wstringstream sstream;
				sstream << "Output A" << (i + 1);
				wstring output = sstream.str();
				if (find(outputs.begin(), outputs.end(), output) != outputs.end())
				{
					auto engine = std::make_unique<FilterEngine>();
					engine->setDeviceInfo(false, true, L"Voicemeeter", output, L"", L"Voicemeeter " + output);
					replacement.engines.push_back(std::move(engine));
				}
				else
				{
					replacement.engines.push_back(nullptr);
				}

				replacement.idleSampleCounts.push_back(0);
			}

			// Finish building first, then initialize and publish atomically with
			// respect to the audio callback.
			engineState.withLock([&](EngineState& state) {
				const float currentSampleRate = sampleRate.load();
				const unsigned currentMaxFrameCount = maxFrameCount.load();
				if (currentSampleRate != 0.0f && currentMaxFrameCount != 0)
				{
					for (const auto& engine : replacement.engines)
						if (engine != nullptr)
							engine->initialize(currentSampleRate, 8, 8, 8, 0, currentMaxFrameCount);
				}
				state = std::move(replacement);
			});
		}
	}
}

void VoicemeeterClient::endSoftware()
{
	if (callbackRegistered && vmr.VBVMR_AudioCallbackUnregister != nullptr)
	{
		vmr.VBVMR_AudioCallbackUnregister();
		callbackRegistered = false;
	}
	if (loggedIn && vmr.VBVMR_Logout != nullptr)
	{
		vmr.VBVMR_Logout();
		loggedIn = false;
	}
}

void VoicemeeterClient::handleCommand(WPARAM wparam, LPARAM lparam)
{
	switch (LOWORD(wparam))
	{
	case IDM_RESTART:
		Sleep(50);
		if (vmr.VBVMR_AudioCallbackStart != nullptr)
			vmr.VBVMR_AudioCallbackStart();
		break;
	}
}

bool VoicemeeterClient::isBufferSilent(float** sampleData, long sampleCount)
{
	bool silent = true;

	for (int j = 0; j < 8; j++)
	{
		const float* buf = sampleData[j];
		for (int k = 0; k < sampleCount; k++)
		{
			if (buf[k] != 0.0f)
			{
				silent = false;
				break;
			}
		}
	}

	return silent;
}

static long __stdcall callback(void* lpUser, long nCommand, void* lpData, long nnn)
{
	VoicemeeterClient* client = (VoicemeeterClient*)lpUser;
	client->handle(nCommand, lpData, nnn);

	return 0;
}
