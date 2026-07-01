/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include "filters/MultiConvolutionCommand.h"
#include "MultiConvolutionFilterGUI.h"
#include "MultiConvolutionFilterGUIFactory.h"
#include "../FilterGUIFactoryRegistry.h"

// Shares the Convolution order: it runs in the same processing-filter slot and
// is told apart by its command keyword, so a "MultiConvolution:" line falls
// through the Convolution factory (whose parse rejects it) to this one.
REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::Convolution, MultiConvolutionFilterGUIFactory)

MultiConvolutionFilterGUIFactory::MultiConvolutionFilterGUIFactory()
{
}

QList<FilterTemplate> MultiConvolutionFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("MultiConvolution (BRIR / multi-input synthesis convolution)"), "MultiConvolution:", QStringList(tr("Advanced filters"))));
	return list;
}

void MultiConvolutionFilterGUIFactory::startOfFile(const QString& configPath)
{
	this->configPath = configPath;
}

IFilterGUI* MultiConvolutionFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	MultiConvolutionCommand cmd;
	if (MultiConvolutionCommand::parse(command.toStdWString(), parameters.toStdWString(), cmd))
		return new MultiConvolutionFilterGUI(configPath, QString::fromStdWString(cmd.outputChannel), QString::fromStdWString(cmd.path));

	return nullptr;
}
