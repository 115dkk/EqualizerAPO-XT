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
	// The programmatic vocabulary the expression parser owns. This factory
	// used to only suppress GUIs for backtick lines, which left conditionals
	// and Eval as typing-only knowledge; the picker offers them like any
	// other command, filed under Control next to Include and Channel.
	QStringList path(tr("Control"));
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("If (Begin conditional section)"), "If: ", path));
	list.append(FilterTemplate(tr("ElseIf (Alternative condition)"), "ElseIf: ", path));
	list.append(FilterTemplate(tr("Else (Fallback section)"), "Else:", path));
	list.append(FilterTemplate(tr("EndIf (End conditional section)"), "EndIf:", path));
	list.append(FilterTemplate(tr("Eval (Evaluate expression)"), "Eval: ", path));
	return list;
}

IFilterGUI* ExpressionFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	// do not create a gui if parameters contain expressions
	if (parameters.contains('`'))
		command = "";

	return nullptr;
}
