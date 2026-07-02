/*
    This file is part of Equalizer APO, a system-wide equalizer.
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
#include <wincrypt.h>
#include "StringHelper.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

void VSTPluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
	if (library->isVST3())
	{
		if (chunkData != L"")
		{
			DWORD bufSize = 0;
			CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, NULL, &bufSize, NULL, NULL);
			vector<char> data(bufSize);
			if (bufSize > 0 && CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, (BYTE*)data.data(), &bufSize, NULL, NULL) == TRUE)
			{
				VST3MemoryStream* stream = new VST3MemoryStream(data);
				if (vst3Component != NULL)
					vst3Component->setState(stream);
				stream->seek(0, IBStream::kIBSeekSet);
				if (vst3Controller != NULL)
					vst3Controller->setState(stream);
				stream->release();
			}
		}
		else if (vst3Controller != NULL)
		{
			for (auto it : paramMap)
			{
				for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
				{
					ParameterInfo info;
					if (vst3Controller->getParameterInfo(i, info) == kResultOk
						&& it.first == wstring((wchar_t*)info.title))
					{
						vst3Controller->setParamNormalized(info.id, it.second);
						break;
					}
				}
			}
		}
		return;
	}

	if (effect == NULL)
		return;

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		if (chunkData != L"")
		{
			DWORD bufSize = 0;
			CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, NULL, &bufSize, NULL, NULL);
			BYTE* buf = new BYTE[bufSize];
			if (CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, buf, &bufSize, NULL, NULL) == TRUE)
				effect->control(effect, VST_EFFECT_OPCODE_SET_CHUNK_DATA, 1, bufSize, buf, 0.0f);
			delete[] buf;
		}
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			wstring name = StringHelper::toWString(buf, CP_UTF8);
			auto it = paramMap.find(name);
			if (it != paramMap.end())
				effect->set_parameter(effect, i, it->second);
		}
	}
}

void VSTPluginInstance::readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const
{
	if (library->isVST3())
	{
		chunkData = L"";
		paramMap.clear();

		if (vst3Component != NULL)
		{
			VST3MemoryStream* stream = new VST3MemoryStream();
			if (vst3Component->getState(stream) == kResultOk && !stream->getData().empty())
			{
				DWORD stringLength = 0;
				CryptBinaryToStringW((BYTE*)stream->getData().data(), (DWORD)stream->getData().size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &stringLength);
				wchar_t* string = new wchar_t[stringLength];
				if (CryptBinaryToStringW((BYTE*)stream->getData().data(), (DWORD)stream->getData().size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, string, &stringLength) == TRUE)
					chunkData = string;
				delete[] string;
			}
			stream->release();
		}

		if (chunkData == L"" && vst3Controller != NULL)
		{
			for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
			{
				ParameterInfo info;
				if (vst3Controller->getParameterInfo(i, info) == kResultOk)
					paramMap[wstring((wchar_t*)info.title)] = (float)vst3Controller->getParamNormalized(info.id);
			}
		}
		return;
	}

	if (effect == NULL)
		return;

	chunkData = L"";
	paramMap.clear();

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		BYTE* chunk = NULL;
		int size = (int)effect->control(effect, VST_EFFECT_OPCODE_GET_CHUNK_DATA, 1, 0, &chunk, 0.0f);
		DWORD stringLength = 0;
		CryptBinaryToStringW(chunk, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &stringLength);
		wchar_t* string = new wchar_t[stringLength];
		if (CryptBinaryToStringW(chunk, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, string, &stringLength) == TRUE)
			chunkData = string;
		delete[] string;
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			float value = effect->get_parameter(effect, i);
			paramMap[StringHelper::toWString(buf, CP_UTF8)] = value;
		}
	}
}
