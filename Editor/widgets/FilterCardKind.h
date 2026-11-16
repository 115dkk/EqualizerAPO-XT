/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	A card row's kind as an enum, for the Editor's branch decisions. The text
	spelling (FilterCardDescriptor::type, CommandRowInfo::type) stays the key
	the rest of the program sees: it reaches QSS dynamic properties, object
	names, gallery scene names and tests, so it keeps its values. Only the
	comparisons go through the enum, so a misspelt kind is a compile error
	instead of a branch that silently never runs (audit #467 TD-49).
	QtCore-only, like CommandRowInfo.h, so EditorLogicTests can use it.
*/

#pragma once

#include <array>

#include <QLatin1String>
#include <QString>

// One enumerator per type FilterCardModel::describeLine can give a row: the
// catalog's types (FilterCommandCatalog), plus "spacer" for a blank line and
// "text" for a line no command claims. "if" covers If/ElseIf/Else/EndIf and
// "convolution" covers Convolution and MultiConvolution, as the type does.
enum class FilterCardKind
{
	Unknown,
	Spacer,
	Text,
	Comment,
	Preamp,
	Delay,
	Hilbert,
	Velvet,
	Biquad,
	GraphicEq,
	Copy,
	Channel,
	NewChannel,
	Send,
	Include,
	Convolution,
	Vst,
	Device,
	Stage,
	Loudness,
	SubwooferRouting,
	If,
	Eval,
};

struct FilterCardKindName
{
	FilterCardKind kind;
	const char* type;
};

// The one table between the enum and the existing type strings.
inline constexpr std::array<FilterCardKindName, 22> filterCardKindNames()
{
	return { {
		{ FilterCardKind::Spacer, "spacer" },
		{ FilterCardKind::Text, "text" },
		{ FilterCardKind::Comment, "comment" },
		{ FilterCardKind::Preamp, "preamp" },
		{ FilterCardKind::Delay, "delay" },
		{ FilterCardKind::Hilbert, "hilbert" },
		{ FilterCardKind::Velvet, "velvet" },
		{ FilterCardKind::Biquad, "biquad" },
		{ FilterCardKind::GraphicEq, "graphiceq" },
		{ FilterCardKind::Copy, "copy" },
		{ FilterCardKind::Channel, "channel" },
		{ FilterCardKind::NewChannel, "newchannel" },
		{ FilterCardKind::Send, "send" },
		{ FilterCardKind::Include, "include" },
		{ FilterCardKind::Convolution, "convolution" },
		{ FilterCardKind::Vst, "vst" },
		{ FilterCardKind::Device, "device" },
		{ FilterCardKind::Stage, "stage" },
		{ FilterCardKind::Loudness, "loudness" },
		{ FilterCardKind::SubwooferRouting, "subwooferrouting" },
		{ FilterCardKind::If, "if" },
		{ FilterCardKind::Eval, "eval" },
	} };
}

// The kind a type string names, matched exactly (case included, as the string
// comparisons it replaces did). Any other string, the empty one included, is
// Unknown.
inline FilterCardKind filterCardKindFromType(const QString& type)
{
	for (const FilterCardKindName& entry : filterCardKindNames())
	{
		if (type == QLatin1String(entry.type))
			return entry.kind;
	}
	return FilterCardKind::Unknown;
}

// The type string of a kind; empty for Unknown.
inline QString filterCardKindType(FilterCardKind kind)
{
	for (const FilterCardKindName& entry : filterCardKindNames())
	{
		if (entry.kind == kind)
			return QLatin1String(entry.type);
	}
	return QString();
}
