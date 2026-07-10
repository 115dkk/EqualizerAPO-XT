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

#include "ExpressionFilterGUIFactory.h"
#include "../FilterGUIFactoryRegistry.h"

REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::Expression, ExpressionFilterGUIFactory)

QList<FilterTemplate> ExpressionFilterGUIFactory::createFilterTemplates()
{
	// The programmatic vocabulary the expression parser owns. Eval files
	// under Control next to Include and Channel; the If family gets its own
	// Branching section, which closes the catalog after Control - the
	// listing order here (Eval before the If family) is what puts Control
	// ahead of Branching among the trailing sections, so keep it.
	QStringList controlPath(tr("Control"));
	QStringList branchingPath(tr("Branching"));
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("Eval (Evaluate expression)"), "Eval: ", controlPath));
	list.append(FilterTemplate(tr("If (Begin conditional section)"), "If: ", branchingPath));
	list.append(FilterTemplate(tr("ElseIf (Alternative condition)"), "ElseIf: ", branchingPath));
	list.append(FilterTemplate(tr("Else (Fallback section)"), "Else:", branchingPath));
	list.append(FilterTemplate(tr("EndIf (End conditional section)"), "EndIf:", branchingPath));
	return list;
}

IFilterGUI* ExpressionFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	// do not create a gui if parameters contain expressions
	if (parameters.contains('`'))
		command = "";

	return nullptr;
}
