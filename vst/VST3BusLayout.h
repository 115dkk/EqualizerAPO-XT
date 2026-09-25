/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <iterator>
#include <span>
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

namespace vst3layoutdetail
{
	// Equalizer APO / Windows channel order for each logical layout. The VST3
	// host maps these semantic slots to the accepted speaker arrangement
	// before handing buffers to the plug-in.
	inline constexpr const wchar_t* mono[] = {L"C"};
	inline constexpr const wchar_t* stereo[] = {L"L", L"R"};
	inline constexpr const wchar_t* surround40[] = {L"L", L"R", L"RL", L"RR"};
	inline constexpr const wchar_t* surround41[] = {L"L", L"R", L"LFE", L"RL", L"RR"};
	inline constexpr const wchar_t* surround50[] = {L"L", L"R", L"C", L"RL", L"RR"};
	inline constexpr const wchar_t* surround51[] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};
	inline constexpr const wchar_t* surround61[] = {L"L", L"R", L"C", L"LFE", L"RC", L"SL", L"SR"};
	inline constexpr const wchar_t* surround71[] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
	inline constexpr const wchar_t* surround712[] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR",
		L"TSL", L"TSR"};
	inline constexpr const wchar_t* surround714[] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR",
		L"TFL", L"TFR", L"TRL", L"TRR"};
}

// The one table of what each layout is on the config side (audit #348
// TD-48): its config token and its channel order, from which the width,
// the parser and every list of layouts are derived. Rows follow the enum
// order. The SDK side of the same layouts - which speaker arrangements stand
// for each one - is the table in VST3SpeakerMapping.cpp, keyed by this enum;
// it stays there so the config model keeps compiling without the SDK.
struct VST3BusLayoutDefinition
{
	VST3BusLayout layout = VST3BusLayout::Auto;
	const wchar_t* name = L"";
	std::span<const wchar_t* const> channelNames;
};

inline constexpr VST3BusLayoutDefinition vst3BusLayoutTable[] = {
	{VST3BusLayout::Auto, L"Auto", {}},
	{VST3BusLayout::Mono, L"Mono", vst3layoutdetail::mono},
	{VST3BusLayout::Stereo, L"Stereo", vst3layoutdetail::stereo},
	{VST3BusLayout::Surround40, L"4.0", vst3layoutdetail::surround40},
	{VST3BusLayout::Surround41, L"4.1", vst3layoutdetail::surround41},
	{VST3BusLayout::Surround50, L"5.0", vst3layoutdetail::surround50},
	{VST3BusLayout::Surround51, L"5.1", vst3layoutdetail::surround51},
	{VST3BusLayout::Surround61, L"6.1", vst3layoutdetail::surround61},
	{VST3BusLayout::Surround71, L"7.1", vst3layoutdetail::surround71},
	{VST3BusLayout::Surround712, L"7.1.2", vst3layoutdetail::surround712},
	{VST3BusLayout::Surround714, L"7.1.4", vst3layoutdetail::surround714}
};

namespace vst3layoutdetail
{
	constexpr bool tableFollowsEnum()
	{
		for (size_t i = 0; i < std::size(vst3BusLayoutTable); i++)
		{
			if (static_cast<size_t>(vst3BusLayoutTable[i].layout) != i)
				return false;
		}
		return true;
	}
	static_assert(tableFollowsEnum(), "vst3BusLayoutTable rows must follow the VST3BusLayout order");
	static_assert(std::size(vst3BusLayoutTable) == static_cast<size_t>(VST3BusLayout::Surround714) + 1,
		"vst3BusLayoutTable must have one row per VST3BusLayout");
}

inline const VST3BusLayoutDefinition& vst3BusLayoutDefinition(VST3BusLayout layout) noexcept
{
	const size_t index = static_cast<size_t>(layout);
	return index < std::size(vst3BusLayoutTable) ? vst3BusLayoutTable[index] : vst3BusLayoutTable[0];
}

// Every layout with a fixed width, in menu order (Auto excluded).
inline std::span<const VST3BusLayoutDefinition> vst3ExplicitBusLayouts() noexcept
{
	return std::span<const VST3BusLayoutDefinition>(vst3BusLayoutTable).subspan(1);
}

inline const wchar_t* vst3BusLayoutName(VST3BusLayout layout) noexcept
{
	return vst3BusLayoutDefinition(layout).name;
}

// 0 for Auto, which follows the device.
inline int vst3BusLayoutChannelCount(VST3BusLayout layout) noexcept
{
	return static_cast<int>(vst3BusLayoutDefinition(layout).channelNames.size());
}

inline bool parseVST3BusLayout(const std::wstring& text, VST3BusLayout& layout) noexcept
{
	for (const VST3BusLayoutDefinition& definition : vst3BusLayoutTable)
	{
		if (text == definition.name)
		{
			layout = definition.layout;
			return true;
		}
	}
	return false;
}

inline std::vector<std::wstring> vst3BusLayoutChannelNames(VST3BusLayout layout)
{
	const std::span<const wchar_t* const> names = vst3BusLayoutDefinition(layout).channelNames;
	return std::vector<std::wstring>(names.begin(), names.end());
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
