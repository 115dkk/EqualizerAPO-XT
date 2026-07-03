/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Thedering

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

#include "stdafx.h"
#include <DeviceAPOInfo.h>
#include <helpers/RegistryHelper.h>
#include <ObjBase.h>
#include <QtWidgets/QApplication>
#include <VoicemeeterAPOInfo.h>
#include <winsock2.h>
#include "ReceiveThread.h"
#include "DeviceSelector.h"
#include "Editor/helpers/QtAppBootstrap.h"

int main(int argc, char* argv[])
{
	int result = 0;

	// Shared bootstrap: anchors the plugin path (a security concern for this
	// elevated process) and, below, applies the language the user picked in
	// the Editor. (audit #146 TD011)
	QtAppBootstrap::addExecutableRelativePluginPath();

	QApplication app(argc, argv);
	if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
		app.setStyle("fusion");

	QtAppBootstrap::applyUserLocale();

	QTranslator qtTranslator;
	QTranslator deviceSelectorTranslator;
	QtAppBootstrap::installTranslators(app, QStringLiteral("DeviceSelector"), qtTranslator, deviceSelectorTranslator);

	if (app.arguments().contains("/u"))
	{
		for (int index = 0; index <= 1; index++)
		{
			std::vector<std::shared_ptr<AbstractAPOInfo>> apoInfos =
				DeviceAPOInfo::loadAllInfos(index == 1);

			for (std::shared_ptr<AbstractAPOInfo>& apoInfo : apoInfos)
			{
				try
				{
					if (apoInfo->isInstalled())
						apoInfo->uninstall();
				}
				catch (const RegistryException& e)
				{
					QMessageBox::critical(nullptr,
						DeviceSelector::tr(
							"Error while accessing the registry"),
						QString::fromStdWString(e.getMessage()));
					result = -1;
				}
			}
		}

		VoicemeeterAPOInfo::ensureVoicemeeterClientRunning();
	}
	else
	{
		DeviceSelector dialog;
		dialog.show();
		result = app.exec();
	}

	return result;
}
