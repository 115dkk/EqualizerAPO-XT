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

#include "filters/DelayCommand.h"
#include "DelayFilterGUI.h"
#include "DelayFilterGUIFactory.h"
#include "../FilterGUIFactoryRegistry.h"

REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::Delay, DelayFilterGUIFactory)

DelayFilterGUIFactory::DelayFilterGUIFactory()
{
}

QList<FilterTemplate> DelayFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	// Grouped with the all-pass rather than with the basic filters: a delay and
	// an all-pass are the two commands that change when sound arrives without
	// changing how loud it is, and they are what a user reaches for together
	// when aligning drivers or correcting a crossover.
	list.append(FilterTemplate(tr("Delay"), "Delay: 0 ms", QStringList(tr("Phase & Time"))));
	return list;
}

IFilterGUI* DelayFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	DelayFilterGUI* result = nullptr;

	if (command == "Delay")
	{
		// Parse through the shared codec rather than the engine factory: the
		// factory's no-op gate rejects a 0 delay (the insert template's own
		// "Delay: 0 ms"), but the GUI must still open for it.
		DelayCommand cmd;
		if (DelayCommand::parse(parameters.toStdWString(), cmd))
			result = new DelayFilterGUI(cmd.delay, cmd.isMs);
	}

	return result;
}
