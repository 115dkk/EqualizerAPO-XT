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

#include "BiQuadFilterGUI.h"
#include "BiQuadFilterGUIFactory.h"
#include <filters/BiQuadFilterFactory.h>
#include "../FilterGUIFactoryRegistry.h"

REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::BiQuad, BiQuadFilterGUIFactory)

BiQuadFilterGUIFactory::BiQuadFilterGUIFactory()
{
}

QList<FilterTemplate> BiQuadFilterGUIFactory::createFilterTemplates()
{
	QStringList path(tr("Parametric filters"));

	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("Peaking filter"), "Filter: ON PK Fc 100 Hz Gain 0 dB Q 10", path));
	list.append(FilterTemplate(tr("Low-pass filter"), "Filter: ON LP Fc 100 Hz", path));
	list.append(FilterTemplate(tr("High-pass filter"), "Filter: ON HP Fc 100 Hz", path));
	list.append(FilterTemplate(tr("Band-pass filter"), "Filter: ON BP Fc 100 Hz Q 10", path));
	list.append(FilterTemplate(tr("Low-shelf filter"), "Filter: ON LS Fc 100 Hz Gain 0 dB", path));
	list.append(FilterTemplate(tr("High-shelf filter"), "Filter: ON HS Fc 100 Hz Gain 0 dB", path));
	list.append(FilterTemplate(tr("Notch filter"), "Filter: ON NO Fc 100 Hz", path));

	// The all-pass sits under Phase & Time, not among the parametric filters.
	// Every other entry in that group changes a level; this one cannot. Listing
	// it beside them is a large part of why it read as a filter that does
	// nothing - a user looking for a level control finds it, tries it, and
	// hears no difference at all.
	//
	// Q 0.707 rather than the 10 this template used to create: Q 10 at 100 Hz
	// concentrates a third of a second of group delay into a narrow band, which
	// is a strange filter to hand someone as a starting point. Existing
	// configurations keep whatever they were written with.
	QStringList phasePath(tr("Phase & Time"));
	list.append(FilterTemplate(tr("1st-order all-pass"), "Filter: ON AP Fc 100 Hz Order 1", phasePath));
	list.append(FilterTemplate(tr("2nd-order all-pass"), "Filter: ON AP Fc 100 Hz Q 0.707 Order 2", phasePath));
	return list;
}

IFilterGUI* BiQuadFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	BiQuadFilterGUI* result = nullptr;

	if (command.startsWith("Filter"))
	{
		// Parse the config line through the engine's single owning parse routine
		// and populate the GUI directly from the resulting command, instead of
		// constructing a throwaway BiQuadFilter just to read its fields back.
		std::wstring commandWStr = command.toStdWString();
		std::wstring paramWStr = parameters.toStdWString();
		BiQuadCommand cmd;
		if (BiQuadFilterFactory::parseCommand(commandWStr, paramWStr, cmd))
			result = new BiQuadFilterGUI(cmd);
	}

	return result;
}
