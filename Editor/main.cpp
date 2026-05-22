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

#include <cstdio>
#include <cstring>
#include <string>

#include <QTranslator>
#include <QApplication>
#include <QDir>
#include <QCommandLineParser>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "CustomStyle.h"
#include "MainWindow.h"
#include "SkinManager.h"
#include "helpers/ApoRegistration.h"
#include "helpers/RegistryHelper.h"
#include "Editor/helpers/GUIHelper.h"


namespace
{
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

bool matchesHook(const char* arg, const char* name)
{
	return std::strcmp(arg, name) == 0;
}

int handleVelopackHook(int argc, char* argv[])
{
	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		if (arg == nullptr || arg[0] != '-')
			continue;

		std::wstring exeDir = executableDirectory();
		if (matchesHook(arg, "--veloapp-install"))
		{
			auto rc = ApoRegistration::install(exeDir);
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-updated"))
		{
			ApoRegistration::stopAudioService();
			auto rc = ApoRegistration::install(exeDir);
			ApoRegistration::startAudioService();
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-obsolete"))
		{
			ApoRegistration::stopAudioService();
			return 0;
		}
		if (matchesHook(arg, "--veloapp-uninstall"))
		{
			auto rc = ApoRegistration::uninstall(exeDir);
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
	}
	return -1;
}

void launchDeviceSelector(const std::wstring& exeDir)
{
	std::wstring deviceSelector = exeDir;
	if (!deviceSelector.empty() && deviceSelector.back() != L'\\' && deviceSelector.back() != L'/')
		deviceSelector.push_back(L'\\');
	deviceSelector += L"DeviceSelector.exe";

	HINSTANCE result = ShellExecuteW(nullptr, L"open", deviceSelector.c_str(), L"/i", exeDir.c_str(), SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		fwprintf(stderr, L"DeviceSelector launch failed (code=%lld)\n", static_cast<long long>(reinterpret_cast<INT_PTR>(result)));
}
}

int main(int argc, char* argv[])
{
	int hookResult = handleVelopackHook(argc, argv);
	if (hookResult >= 0)
		return hookResult;

	int result = -1;
#ifdef _DEBUG
	// _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	// _CrtSetBreakAlloc(3318);
#endif

	QCoreApplication::addLibraryPath("qt");
	qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");

	bool restart;
	do
	{
		QApplication application(argc, argv);
		application.setStyle(new CustomStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		{
			QString skinId = settings.value(QStringLiteral("interface/skin"), QStringLiteral("glassy")).toString();
			bool dark = settings.value(QStringLiteral("interface/dark"), GUIHelper::isDarkMode()).toBool();
			SkinManager::instance()->applySkin(skinId, dark);
			const SkinTokens& tokens = SkinManager::instance()->tokens();
			QPalette palette = application.palette();
			QColor background(tokens.background);
			QColor surface(tokens.surface);
			QColor card(tokens.card);
			QColor text(tokens.text);
			QColor accent(tokens.accent);
			palette.setColor(QPalette::Window, background);
			palette.setColor(QPalette::WindowText, text);
			palette.setColor(QPalette::Base, surface);
			palette.setColor(QPalette::AlternateBase, card);
			palette.setColor(QPalette::Text, text);
			palette.setColor(QPalette::Button, card);
			palette.setColor(QPalette::ButtonText, text);
			palette.setColor(QPalette::ToolTipBase, card);
			palette.setColor(QPalette::ToolTipText, text);
			palette.setColor(QPalette::Highlight, accent);
			palette.setColor(QPalette::HighlightedText, dark ? QColor(QStringLiteral("#0c0c16")) : QColor(QStringLiteral("#ffffff")));
			palette.setColor(QPalette::PlaceholderText, QColor(tokens.mutedText));
			palette.setColor(QPalette::Light, card.lighter(120));
			palette.setColor(QPalette::Midlight, card.lighter(105));
			palette.setColor(QPalette::Mid, surface);
			palette.setColor(QPalette::Dark, background.darker(120));
			palette.setColor(QPalette::Shadow, background.darker(160));
			palette.setColor(QPalette::Link, accent);
			palette.setColor(QPalette::LinkVisited, accent.darker(110));
			application.setPalette(palette);
		}

		QVariant languageValue = settings.value("language");
		if (languageValue.isValid())
			QLocale::setDefault(QLocale(languageValue.toString()));
		else
			QLocale::setDefault(QLocale::system());

		QTranslator qtTranslator;
		if (qtTranslator.load(QLocale(), ":/translations/qtbase", "_"))
			application.installTranslator(&qtTranslator);

		QTranslator editorTranslator;
		if (editorTranslator.load(QLocale(), ":/translations/Editor", "_"))
			application.installTranslator(&editorTranslator);

		QString configPath = QDir::currentPath();
		if (RegistryHelper::keyExists(APP_REGPATH) && RegistryHelper::valueExists(APP_REGPATH, L"ConfigPath"))
			configPath = QString::fromStdWString(RegistryHelper::readValue(APP_REGPATH, L"ConfigPath"));
		QDir configDir(configPath);

		if (!RegistryHelper::keyExists(USER_REGPATH))
			RegistryHelper::createKey(USER_REGPATH);

		if (!RegistryHelper::keyExists(EDITOR_REGPATH))
			RegistryHelper::createKey(EDITOR_REGPATH);

		if (!RegistryHelper::keyExists(EDITOR_PER_FILE_REGPATH))
			RegistryHelper::createKey(EDITOR_PER_FILE_REGPATH);

		MainWindow w(configDir);
		w.show();

		QCommandLineParser parser;
		parser.process(application);
		QStringList args = parser.positionalArguments();
		if (args.isEmpty() && w.isEmpty())
			args = QStringList("config.txt");

		for (const QString& arg : args)
			w.load(configDir.absoluteFilePath(arg));

		bool firstRun = !qEnvironmentVariableIsEmpty("VELOPACK_FIRSTRUN");
		if (firstRun)
			launchDeviceSelector(executableDirectory());
		else
			w.doChecks();

		result = application.exec();

		restart = w.shouldRestart();
	}
	while (restart);

	return result;
}
