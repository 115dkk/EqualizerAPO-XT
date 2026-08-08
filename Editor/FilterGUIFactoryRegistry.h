/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#pragma once

#include <memory>
#include <vector>

#include <QList>

class IFilterGUIFactory;

using FilterGUIFactoryCreator = std::unique_ptr<IFilterGUIFactory>(*)();

// Single source of truth for the order in which FilterTable offers each parsed
// configuration line to the GUI factories. Factories are sorted by ascending
// order (lower runs first) in FilterGUIFactoryRegistry::createFactories(); the
// first factory whose createFilterGUI() handles the line wins, so the order is
// significant and these constants preserve the historical sequence.
//
// This mirrors the engine's FilterFactoryPriority (filters/FilterFactoryRegistry.h)
// but the Editor roster is its own set: it has a Comment GUI and folds every
// "Filter ..." line into one BiQuad GUI rather than the engine's IIR+BiQuad pair.
// Changing a value here changes the matching order, so keep the numbering
// contiguous and intentional.
namespace FilterGUIFactoryOrder
{
	constexpr int Expression = 0;
	constexpr int Comment = 1;
	constexpr int Include = 2;
	constexpr int Device = 3;
	constexpr int Channel = 4;
	constexpr int Stage = 5;
	constexpr int Preamp = 6;
	constexpr int BiQuad = 7;
	constexpr int Delay = 8;
	constexpr int Copy = 9;
	constexpr int GraphicEQ = 10;
	constexpr int Convolution = 11;
	constexpr int Spatial = 12;
	constexpr int VSTPlugin = 13;
	constexpr int LoudnessCorrection = 14;
	constexpr int SubwooferRouting = 15;
}

class FilterGUIFactoryRegistry
{
public:
	static bool registerFactory(int order, FilterGUIFactoryCreator creator);

	// Fresh factory instances in ascending order. Ownership is explicit in the
	// returned collection and transfers to FilterTable by move.
	static std::vector<std::unique_ptr<IFilterGUIFactory>> createFactories();
};

// Registers a GUI factory with its matching order. The factory's .cpp is compiled
// straight into the Editor executable (not an archive), so its static initializer
// always runs and no /WHOLEARCHIVE is needed. Each REGISTER line is the single
// place a filter GUI joins the roster.
#define REGISTER_FILTER_GUI_FACTORY(order, factoryType) \
	namespace \
	{ \
		const bool factoryType##Registered = FilterGUIFactoryRegistry::registerFactory(order, []() -> std::unique_ptr<IFilterGUIFactory> { return std::make_unique<factoryType>(); }); \
	}
