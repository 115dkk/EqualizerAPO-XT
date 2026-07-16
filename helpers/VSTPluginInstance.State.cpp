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
#include <limits>
#include <wincrypt.h>
#include "StringHelper.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/smartpointer.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
	bool decodeBase64(const wstring& encoded, vector<char>& decoded)
	{
		DWORD requiredSize = 0;
		if (CryptStringToBinaryW(
			encoded.c_str(),
			0,
			CRYPT_STRING_BASE64,
			NULL,
			&requiredSize,
			NULL,
			NULL) != TRUE)
		{
			return false;
		}

		vector<char> result(requiredSize);
		DWORD actualSize = requiredSize;
		if (requiredSize != 0 && CryptStringToBinaryW(
			encoded.c_str(),
			0,
			CRYPT_STRING_BASE64,
			reinterpret_cast<BYTE*>(result.data()),
			&actualSize,
			NULL,
			NULL) != TRUE)
		{
			return false;
		}

		result.resize(actualSize);
		decoded = move(result);
		return true;
	}

	bool encodeBase64(const void* data, size_t size, wstring& encoded)
	{
		if (size > (numeric_limits<DWORD>::max)())
			return false;

		const DWORD binarySize = static_cast<DWORD>(size);
		DWORD requiredLength = 0;
		if (CryptBinaryToStringW(
			static_cast<const BYTE*>(data),
			binarySize,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			NULL,
			&requiredLength) != TRUE)
		{
			return false;
		}

		vector<wchar_t> result(requiredLength);
		DWORD actualLength = requiredLength;
		if (requiredLength == 0 || CryptBinaryToStringW(
			static_cast<const BYTE*>(data),
			binarySize,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			result.data(),
			&actualLength) != TRUE)
		{
			return false;
		}

		encoded.assign(result.data());
		return true;
	}
}

void VSTPluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
	if (library->isVST3())
	{
		if (chunkData != L"")
		{
			vector<char> data;
			if (decodeBase64(chunkData, data) && !data.empty())
			{
				auto stream = IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream(data));
				if (vst3Component != NULL)
					vst3Component->setState(stream.get());
				stream->seek(0, IBStream::kIBSeekSet);
				if (vst3Controller != NULL)
					vst3Controller->setState(stream.get());
			}
		}
		else if (vst3Controller != NULL)
		{
			for (const auto& it : paramMap)
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
			vector<char> data;
			if (decodeBase64(chunkData, data))
				effect->control(effect.get(), VST_EFFECT_OPCODE_SET_CHUNK_DATA, 1, data.size(), data.data(), 0.0f);
		}
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect.get(), VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			wstring name = StringHelper::toWString(buf, CP_UTF8);
			auto it = paramMap.find(name);
			if (it != paramMap.end())
				effect->set_parameter(effect.get(), i, it->second);
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
			auto stream = IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream());
			if (vst3Component->getState(stream.get()) == kResultOk && !stream->getData().empty())
				encodeBase64(stream->getData().data(), stream->getData().size(), chunkData);
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
		int size = (int)effect->control(effect.get(), VST_EFFECT_OPCODE_GET_CHUNK_DATA, 1, 0, &chunk, 0.0f);
		if (chunk != NULL && size > 0)
			encodeBase64(chunk, static_cast<size_t>(size), chunkData);
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect.get(), VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			float value = effect->get_parameter(effect.get(), i);
			paramMap[StringHelper::toWString(buf, CP_UTF8)] = value;
		}
	}
}
