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
#include "services/settings/EditorSettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTranslator>

#include "services/registry/WindowsRegistry.h"
#include "services/logging/TaggedLogger.h"
#include "QtAppBootstrap.h"

namespace
{
QtMessageHandler previousMessageHandler = nullptr;

void forwardToProductLog(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	const wchar_t* level = nullptr;
	switch (type)
	{
	case QtWarningMsg:
		level = L"WARN";
		break;
	case QtCriticalMsg:
		level = L"ERR";
		break;
	case QtFatalMsg:
		level = L"FATAL";
		break;
	default:
		break;
	}
	if (level != nullptr)
	{
		constexpr logging::TaggedLogger logLine(L"Qt");
		logLine(level, L"%s", reinterpret_cast<const wchar_t*>(message.utf16()));
	}

	// Qt answers the install with its own default handler when none was set,
	// so the console and debugger output stays what it was.
	if (previousMessageHandler != nullptr)
		previousMessageHandler(type, context, message);
}
}

namespace QtAppBootstrap
{

void installMessageHandler()
{
	QtMessageHandler previous = qInstallMessageHandler(forwardToProductLog);
	// A second call would otherwise make the handler forward to itself.
	if (previous != forwardToProductLog)
		previousMessageHandler = previous;
}

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
	QVariant languageValue = settings.value(QLatin1String(EditorSettings::Keys::Language));
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
