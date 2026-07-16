/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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

#include "filters/loudnessCorrection/LoudnessCorrectionCommand.h"
#include "LoudnessCorrectionFilterGUI.h"
#include "LoudnessCorrectionFilterGUIFactory.h"
#include "../FilterGUIFactoryRegistry.h"

REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::LoudnessCorrection, LoudnessCorrectionFilterGUIFactory)

LoudnessCorrectionFilterGUIFactory::LoudnessCorrectionFilterGUIFactory()
{
}

LoudnessCorrectionFilterGUIFactory::~LoudnessCorrectionFilterGUIFactory()
{
	if (timer != nullptr)
		timer->stop();

}

void LoudnessCorrectionFilterGUIFactory::initialize(FilterTable* filterTable)
{
	this->filterTable = filterTable;
}

QList<FilterTemplate> LoudnessCorrectionFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("Loudness correction"), "LoudnessCorrection: State 1 ReferenceLevel 0 ReferenceOffset 0 Attenuation 1.0", QStringList(tr("Advanced filters"))));
	return list;
}

IFilterGUI* LoudnessCorrectionFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	LoudnessCorrectionFilterGUI* result = nullptr;

	// Parse through the shared codec so the GUI accepts exactly what the
	// engine accepts.
	LoudnessCorrectionCommand cmd;
	if (LoudnessCorrectionCommand::parse(command.toStdWString(), parameters.toStdWString(), cmd))
	{
		result = new LoudnessCorrectionFilterGUI(cmd.referenceLevel, cmd.referenceOffset, cmd.attenuation);

		if (timer == nullptr)
		{
			timer = new QTimer(this);
			connect(timer, SIGNAL(timeout()), this, SLOT(checkVolume()));
			timer->start(10);
		}
	}

	return result;
}

void LoudnessCorrectionFilterGUIFactory::checkVolume()
{
	if (volumeController == nullptr)
	{
		volumeController = std::make_unique<VolumeController>();
		volumeController->getVolume(lastVolume);
	}
	else
	{
		double volume;
		HRESULT res = volumeController->getVolume(volume);

		if (SUCCEEDED(res) && volume != lastVolume)
		{
			filterTable->updateAnalysis();
			lastVolume = volume;
		}
	}
}
