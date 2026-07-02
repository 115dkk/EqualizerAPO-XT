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

namespace
{
// addLibraryPath() resolves a relative path against the current working
// directory, not the executable directory. DeviceSelector runs elevated
// (requireAdministrator), so a relative "qt" plugin path let a caller that
// controls the working directory plant a Qt plugin DLL that the QApplication
// constructor would then load into this elevated process. Anchor the plugin
// search to the executable's own directory instead (mirrors Editor/main.cpp).
std::wstring executableDirectory()
{
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (length == 0)
		return std::wstring();
	std::wstring path(buffer, length);
	size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return path;
	return path.substr(0, slash);
}

void addExecutableRelativePluginPath()
{
	std::wstring pluginDir = executableDirectory();
	if (!pluginDir.empty())
	{
		pluginDir += L"\\qt";
		QCoreApplication::addLibraryPath(QString::fromStdWString(pluginDir));
	}
	else
	{
		QCoreApplication::addLibraryPath(QStringLiteral("qt"));
	}
}
}

int main(int argc, char* argv[])
{
	int result = 0;

	addExecutableRelativePluginPath();

	QApplication app(argc, argv);
	if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
		app.setStyle("fusion");

	QLocale::setDefault(QLocale::system());

	QTranslator qtTranslator;
	if (qtTranslator.load(QLocale(), ":/translations/qtbase", "_"))
		app.installTranslator(&qtTranslator);

	QTranslator deviceSelectorTranslator;
	if (deviceSelectorTranslator.load(
		QLocale(), ":/translations/DeviceSelector", "_"))
		app.installTranslator(&deviceSelectorTranslator);

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
