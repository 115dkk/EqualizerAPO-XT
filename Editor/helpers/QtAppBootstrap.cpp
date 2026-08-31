/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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

#include <string>
#include "platform/windows/WindowsPath.h"
#include "services/registry/RegistryPaths.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTranslator>

#include "services/registry/WindowsRegistry.h"
#include "QtAppBootstrap.h"

namespace QtAppBootstrap
{

void addExecutableRelativePluginPath()
{
	std::wstring pluginDir = pathutil::exeDirectory();
	if (!pluginDir.empty())
	{
		pluginDir += L"\\qt";
		QCoreApplication::addLibraryPath(QString::fromStdWString(pluginDir));
	}
}

void applyUserLocale()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QVariant languageValue = settings.value(QStringLiteral("language"));
	if (languageValue.isValid())
		QLocale::setDefault(QLocale(languageValue.toString()));
	else
		QLocale::setDefault(QLocale::system());
}

void installTranslators(QCoreApplication& app, const QString& catalogName, QTranslator& qtTranslator, QTranslator& appTranslator)
{
	if (qtTranslator.load(QLocale(), QStringLiteral(":/translations/qtbase"), QStringLiteral("_")))
		app.installTranslator(&qtTranslator);

	if (appTranslator.load(QLocale(), QStringLiteral(":/translations/") + catalogName, QStringLiteral("_")))
		app.installTranslator(&appTranslator);
}

}
