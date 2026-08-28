/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <string>
#include <vector>

// Logical main-bus layouts accepted by VSTPlugin's Input/Output keys.
// These are deliberately independent of Steinberg SDK types: the config model
// can be parsed and serialized without pulling the VST3 host ABI into callers.
enum class VST3BusLayout
{
	Auto,
	Mono,
	Stereo,
	Surround40,
	Surround41,
	Surround50,
	Surround51,
	Surround61,
	Surround71,
	Surround712,
	Surround714
};

inline const wchar_t* vst3BusLayoutName(VST3BusLayout layout) noexcept
{
	switch (layout)
	{
	case VST3BusLayout::Auto: return L"Auto";
	case VST3BusLayout::Mono: return L"Mono";
	case VST3BusLayout::Stereo: return L"Stereo";
	case VST3BusLayout::Surround40: return L"4.0";
	case VST3BusLayout::Surround41: return L"4.1";
	case VST3BusLayout::Surround50: return L"5.0";
	case VST3BusLayout::Surround51: return L"5.1";
	case VST3BusLayout::Surround61: return L"6.1";
	case VST3BusLayout::Surround71: return L"7.1";
	case VST3BusLayout::Surround712: return L"7.1.2";
	case VST3BusLayout::Surround714: return L"7.1.4";
	}
	return L"Auto";
}

inline int vst3BusLayoutChannelCount(VST3BusLayout layout) noexcept
{
	switch (layout)
	{
	case VST3BusLayout::Auto: return 0;
	case VST3BusLayout::Mono: return 1;
	case VST3BusLayout::Stereo: return 2;
	case VST3BusLayout::Surround40: return 4;
	case VST3BusLayout::Surround41:
	case VST3BusLayout::Surround50: return 5;
	case VST3BusLayout::Surround51: return 6;
	case VST3BusLayout::Surround61: return 7;
	case VST3BusLayout::Surround71: return 8;
	case VST3BusLayout::Surround712: return 10;
	case VST3BusLayout::Surround714: return 12;
	}
	return 0;
}

inline bool parseVST3BusLayout(const std::wstring& text, VST3BusLayout& layout) noexcept
{
	if (text == L"Auto") layout = VST3BusLayout::Auto;
	else if (text == L"Mono") layout = VST3BusLayout::Mono;
	else if (text == L"Stereo") layout = VST3BusLayout::Stereo;
	else if (text == L"4.0") layout = VST3BusLayout::Surround40;
	else if (text == L"4.1") layout = VST3BusLayout::Surround41;
	else if (text == L"5.0") layout = VST3BusLayout::Surround50;
	else if (text == L"5.1") layout = VST3BusLayout::Surround51;
	else if (text == L"6.1") layout = VST3BusLayout::Surround61;
	else if (text == L"7.1") layout = VST3BusLayout::Surround71;
	else if (text == L"7.1.2") layout = VST3BusLayout::Surround712;
	else if (text == L"7.1.4") layout = VST3BusLayout::Surround714;
	else return false;
	return true;
}

// Equalizer APO / Windows channel order for each logical layout. The VST3 host
// maps these semantic slots to the accepted speaker arrangement before handing
// buffers to the plug-in.
inline std::vector<std::wstring> vst3BusLayoutChannelNames(VST3BusLayout layout)
{
	switch (layout)
	{
	case VST3BusLayout::Mono: return {L"C"};
	case VST3BusLayout::Stereo: return {L"L", L"R"};
	case VST3BusLayout::Surround40: return {L"L", L"R", L"RL", L"RR"};
	case VST3BusLayout::Surround41: return {L"L", L"R", L"LFE", L"RL", L"RR"};
	case VST3BusLayout::Surround50: return {L"L", L"R", L"C", L"RL", L"RR"};
	case VST3BusLayout::Surround51: return {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};
	case VST3BusLayout::Surround61: return {L"L", L"R", L"C", L"LFE", L"RC", L"SL", L"SR"};
	case VST3BusLayout::Surround71:
		return {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
	case VST3BusLayout::Surround712:
		return {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR", L"TSL", L"TSR"};
	case VST3BusLayout::Surround714:
		return {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR",
			L"TFL", L"TFR", L"TRL", L"TRR"};
	case VST3BusLayout::Auto: return {};
	}
	return {};
}

struct VST3BusContract
{
	VST3BusLayout input = VST3BusLayout::Auto;
	VST3BusLayout output = VST3BusLayout::Auto;

	bool hasExplicitLayout() const noexcept
	{
		return input != VST3BusLayout::Auto || output != VST3BusLayout::Auto;
	}
};
