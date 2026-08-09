/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#include "stdafx.h"

#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/TextEncoding.h"

std::wstring wintext::toWideString(const std::string& value, unsigned codePage)
{
	const int length = MultiByteToWideChar(codePage, 0, value.c_str(), -1, nullptr, 0);
	if (length == 0)
		return L"";
	std::vector<wchar_t> buffer(length);
	MultiByteToWideChar(codePage, 0, value.c_str(), -1, buffer.data(), length);
	return buffer.data();
}

std::string wintext::toNarrowString(const std::wstring& value, unsigned codePage)
{
	const int length = WideCharToMultiByte(codePage, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (length == 0)
		return "";
	std::vector<char> buffer(length);
	WideCharToMultiByte(codePage, 0, value.c_str(), -1, buffer.data(), length, nullptr, nullptr);
	return buffer.data();
}
