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
#include "services/registry/RegistryPaths.h"
#include <devices/DeviceAPOInfo.h>
#include <services/registry/WindowsRegistry.h>
#include <ObjBase.h>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QRegularExpression>
#include <cstdarg>
#include <cstring>
#include <QFontDatabase>
#include <QSettings>
#include <QStyleFactory>
#include <QtWidgets/QApplication>
#include <devices/VoicemeeterAPOInfo.h>
#include <winsock2.h>
#include "ReceiveThread.h"
#include "DeviceSelector.h"
#include "DeviceTestThread.h"
#include <devices/DeviceAPOInfoKeys.h>
#include <services/windows/WindowsService.h>
#include "PreviewDevices.h"
#include "skins/DeviceSkinPainter.h"
#include "Editor/helpers/QtAppBootstrap.h"
#include "Editor/helpers/EditorSettings.h"
#include "Editor/skins/SkinThemeData.h"
#include "services/install/ApoRegistration.h"
#include "services/diagnostics/InstallDiagnostics.h"
#include "services/logging/Logging.h"

namespace
{
// The skin the user picked in the Editor (registry interface/skin +
// interface/dark; both default to the Editor's own defaults, so a machine
// that never chose gets Studio). In the Editor's heritage mode (legacyRows)
// the dialog keeps its classic native look, matching the Editor's choice.
void applyEditorTheme(QApplication& app)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	if (settings.value(QLatin1String(EditorSettings::Keys::LegacyRows), false).toBool())
	{
		// Neutral base forms in classic light colours for the painted chrome;
		// the stock sub-widgets keep the native style.
		DeviceSkinPainter::setHeritageTheme();
		if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
			app.setStyle(QStringLiteral("fusion"));
		return;
	}

	const bool systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
	const EditorSettings::SkinChoice choice = EditorSettings::readSkinChoice(settings, systemDark);
	SkinThemeData::applyToApplication(app, choice.id, choice.dark);
	DeviceSkinPainter::setActiveTheme(choice.id, choice.dark);
}

// --skin-shots <outDir>: renders the dialog with canned devices for every
// skin x dark/light in three states (rest, hovered row, troubleshooting
// open) on the offscreen platform. The review gate's capture source and the
// skin work's regression harness - no registry writes, no COM. Renders in
// the user's language (translators install before the harness runs), so
// byte-comparison only holds for a fixed language setting.
int runSkinShots(QApplication& app)
{
	const QStringList args = app.arguments();
	const int flagIndex = args.indexOf(QStringLiteral("--skin-shots"));
	if (flagIndex < 0 || flagIndex + 1 >= args.size())
	{
		fprintf(stderr, "usage: DeviceSelector --skin-shots <outDir>\n");
		return 2;
	}
	QDir outDir(args[flagIndex + 1]);
	if (!outDir.exists() && !QDir().mkpath(outDir.absolutePath()))
	{
		fprintf(stderr, "DeviceSelector shots: cannot create %s\n", qPrintable(outDir.absolutePath()));
		return 2;
	}

	int failures = 0;
	// From the roster, so a new skin appears in the review captures without
	// anybody remembering this list.
	const QStringList skins = SkinThemeData::ids();
	for (const QString& skinId : skins)
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
			SkinThemeData::applyToApplication(app, skinId, dark);
			DeviceSkinPainter::setActiveTheme(skinId, dark);

			DeviceSelector dialog(PreviewDevices::playback(), PreviewDevices::capture());
			// The size the dialog would open at, before the harness pins its
			// own: the one number the "opens too narrow" report is about.
			if (skinId == skins.first() && !dark)
				fprintf(stderr, "DeviceSelector shots: initial size %dx%d\n", dialog.width(), dialog.height());
			dialog.resize(760, 700);
			dialog.show();
			QApplication::processEvents();
			// One pending install so the will-install state shows.
			dialog.previewCheckDevice(0, 1);
			QApplication::processEvents();

			const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
			auto save = [&](const QString& state) {
				const QString file = outDir.filePath(
					QStringLiteral("devsel_%1_%2_%3.png").arg(skinId, mode, state));
				if (!dialog.grab().save(file))
				{
					fprintf(stderr, "DeviceSelector shots: failed to save %s\n", qPrintable(file));
					failures++;
				}
			};

			save(QStringLiteral("normal"));
			dialog.previewHoverDevice(0, 2);
			QApplication::processEvents();
			save(QStringLiteral("hover"));
			dialog.previewSelectDevice(0, 0);
			dialog.previewOpenTroubleshooting();
			QApplication::processEvents();
			save(QStringLiteral("options"));
			// The ASIO target's own options page (the installed ASIO row of
			// the preview roster), for the judging material.
			dialog.previewSelectDevice(0, 4);
			QApplication::processEvents();
			save(QStringLiteral("asio"));
			// Buffer removal ticked: the wait time unfolds beside it.
			dialog.previewRemoveBuffer();
			QApplication::processEvents();
			save(QStringLiteral("asiowait"));
		}
	}

	fprintf(stderr, "DeviceSelector shots: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}

// --diagnose: writes the install-path report. Read-only and unelevated, because
// the point of it is to be able to look before deciding whether to change
// anything.
//
// The report goes to a file and, when this process was started from a console, to
// that console as well. DeviceSelector is a GUI-subsystem executable, so its
// stdout is not connected to the shell that launched it until we attach: without
// AttachConsole a user running "DeviceSelector --diagnose" from PowerShell would
// see nothing at all and reasonably conclude the switch does not work.
int runDiagnose(QApplication& app)
{
	Q_UNUSED(app);

	const std::wstring reportPath = InstallDiagnostics::writeReport();
	if (reportPath.empty())
	{
		// The console form already printed everything; a message box is only
		// needed when there is no console and nothing on disk either, which means
		// the report went nowhere.
		QMessageBox::warning(nullptr, QStringLiteral("Equalizer APO"),
			QStringLiteral("The diagnostics report could not be written."));
		return 1;
	}

	// Started by double-click or from the Start menu: the path is the only useful
	// thing to say, and a modal box is the only place to say it. Harmless when a
	// console was attached as well - the same content is in both.
	QMessageBox::information(nullptr, QStringLiteral("Equalizer APO"),
		QStringLiteral("Diagnostics written to\n%1")
			.arg(QDir::toNativeSeparators(QString::fromStdWString(reportPath))));
	return 0;
}

// Console output for the headless commands. DeviceSelector is a GUI-subsystem
// executable, so nothing it prints reaches the shell that launched it until
// the parent console is attached - the same step runDiagnose's report takes.
class ConsoleAttachment
{
public:
	ConsoleAttachment()
	{
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
			return;
		attached = true;
		FILE* stream = nullptr;
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);
	}

	~ConsoleAttachment()
	{
		fflush(stdout);
		fflush(stderr);
		if (attached)
			FreeConsole();
	}

private:
	bool attached = false;
};

QString plainText(const QString& html)
{
	QString text = html;
	text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
	return text;
}

// The headless commands' output goes to the attached console and to
// DeviceSelector.log both: a CI runner has no console to attach to, and the
// log is what its job uploads.
void say(const wchar_t* format, ...)
{
	wchar_t line[2048];
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(line, _TRUNCATE, format, args);
	va_end(args);
	fputws(line, stderr);
	size_t length = wcslen(line);
	while (length > 0 && (line[length - 1] == L'\n' || line[length - 1] == L'\r'))
		line[--length] = L'\0';
	LogFStatic(L"%s", line);
}

// --install-endpoint {guid} [--install-mode lfx-gfx|sfx-mfx|sfx-efx]
//                           [--no-original-apo] [--no-test]
// --uninstall-endpoint {guid}
//
// The dialog's OK for one endpoint, without the dialog: the same
// DeviceAPOInfo::install / uninstall, and after an install the same device
// test the dialog runs (DeviceTestThread: restart the audio service, open a
// stream on the endpoint, wait for the APO to report Initialize through the
// test pipe, fall back through the install modes when it does not). Exit 0
// means the APO was seen alive on the endpoint. For the capture gate in CI,
// which needs the product's own install path on a real endpoint, and for a
// support session over a terminal. Elevation is required, as for the dialog.
int runEndpointCommand(QApplication& app, bool install)
{
	ConsoleAttachment console;
	const QStringList args = app.arguments();
	const QString flag = install ? QStringLiteral("--install-endpoint") : QStringLiteral("--uninstall-endpoint");
	const int flagIndex = args.indexOf(flag);
	if (flagIndex < 0 || flagIndex + 1 >= args.size())
	{
		say(L"usage: DeviceSelector %hs {endpoint-guid} [--install-mode lfx-gfx|sfx-mfx|sfx-efx] [--no-original-apo] [--no-test]\n", qPrintable(flag));
		return 2;
	}
	const std::wstring guid = args[flagIndex + 1].toStdWString();

	std::shared_ptr<DeviceAPOInfo> info = std::make_shared<DeviceAPOInfo>();
	try
	{
		if (!info->load(guid))
		{
			say(L"endpoint %s is not present\n", guid.c_str());
			return 2;
		}
	}
	catch (const RegistryError& e)
	{
		say(L"could not read endpoint %s: %s\n", guid.c_str(), e.getMessage().c_str());
		return 1;
	}
	say(L"%s %s: %hs, %hs\n", info->getConnectionName().c_str(), info->getDeviceName().c_str(),
		info->isInput() ? "capture" : "playback", info->isInstalled() ? "installed" : "not installed");

	DeviceAPOInfo::InstallState& state = info->getSelectedInstallState();
	state = info->getCurrentInstallState();
	const int modeIndex = args.indexOf(QStringLiteral("--install-mode"));
	if (modeIndex >= 0 && modeIndex + 1 < args.size())
	{
		const QString mode = args[modeIndex + 1].toLower();
		if (mode == QLatin1String("lfx-gfx"))
			state.installMode = DeviceAPOInfo::INSTALL_LFX_GFX;
		else if (mode == QLatin1String("sfx-mfx"))
			state.installMode = DeviceAPOInfo::INSTALL_SFX_MFX;
		else if (mode == QLatin1String("sfx-efx"))
			state.installMode = DeviceAPOInfo::INSTALL_SFX_EFX;
		else
		{
			say(L"unknown install mode %hs\n", qPrintable(mode));
			return 2;
		}
		// A named mode is a decision; the test must not wander off it.
		state.autoAdjust = false;
	}
	if (args.contains(QStringLiteral("--no-original-apo")))
	{
		state.useOriginalAPOPreMix = false;
		state.useOriginalAPOPostMix = false;
	}

	try
	{
		if (install)
		{
			if (info->isInstalled())
				info->reinstall();
			else
				info->install();
		}
		else
		{
			if (!info->isInstalled())
			{
				say(L"nothing to uninstall\n");
				return 0;
			}
			info->uninstall();
		}
	}
	catch (const RegistryError& e)
	{
		say(L"%s\n", e.getMessage().c_str());
		return 1;
	}
	catch (const DeviceException& e)
	{
		say(L"%s\n", e.getMessage().c_str());
		return 1;
	}
	say(L"%s\n", info->getLastOperationReport().toSummaryLine().c_str());

	if (!install || args.contains(QStringLiteral("--no-test")))
	{
		// The change takes effect when the audio service rebuilds its
		// graph, which the device test would otherwise do.
		try
		{
			WindowsServiceControl::restart(audioServiceName);
		}
		catch (const WindowsServiceError& e)
		{
			say(L"audio service restart failed: %s\n", e.getMessage().c_str());
			return 1;
		}
		return 0;
	}

	QVector<std::shared_ptr<DeviceAPOInfo>> devices;
	devices.append(info);
	DeviceTestThread thread(nullptr, devices);
	bool aborted = false;
	QObject::connect(&thread, &DeviceTestThread::log, [](const QString& message) {
		say(L"test: %hs\n", qPrintable(plainText(message)));
	});
	QObject::connect(&thread, &DeviceTestThread::logError, [](const QString& message) {
		say(L"test error: %hs\n", qPrintable(plainText(message)));
	});
	QObject::connect(&thread, &DeviceTestThread::showErrorDialog, [](const QString& message) {
		say(L"test error: %hs\n", qPrintable(plainText(message)));
	});
	QObject::connect(&thread, &DeviceTestThread::abort, [&aborted](const QString& message, int) {
		aborted = true;
		say(L"test aborted: %hs\n", qPrintable(plainText(message)));
	});
	QObject::connect(&thread, &DeviceTestThread::setItemStatus, [](const QString& deviceGuid, bool postMix, ItemStatusType status) {
		static const char* const names[] = {"waiting", "success", "warning", "error"};
		say(L"test status: %hs %hs %hs\n", qPrintable(deviceGuid), postMix ? "post-mix" : "pre-mix", names[static_cast<int>(status)]);
	});
	QEventLoop loop;
	QObject::connect(&thread, &DeviceTestThread::finished, &loop, &QEventLoop::quit);
	thread.start();
	loop.exec();
	thread.wait();

	const bool ok = !aborted && thread.nonWorkingDeviceCount() == 0;
	say(L"device test: %hs\n", ok ? "the APO is alive on the endpoint" : "the APO did not come up on the endpoint");
	return ok ? 0 : 1;
}
}

int main(int argc, char* argv[])
{
	int result = 0;

	// Before anything else, because this is the process that performs the APO
	// install and until now it wrote no log at all. A user reporting that
	// installing did nothing left behind an Editor.log with no mention of the
	// install, because the Editor was not the program that ran it.
	if (!Logging::useUserFile(L"DeviceSelector.log", true, false, false))
		Logging::useDefaultApoLog();

	// Shared bootstrap: anchors the plugin path (a security concern for this
	// elevated process) and, below, applies the language the user picked in
	// the Editor.
	QtAppBootstrap::addExecutableRelativePluginPath();

	QApplication app(argc, argv);

	// Language first, exactly like the live dialog: the Editor's language
	// choice from the registry, or the system locale when none was made. The
	// shot harness renders through the same translators, so the review
	// captures read in the user's language.
	QtAppBootstrap::applyUserLocale();
	QTranslator qtTranslator;
	QTranslator deviceSelectorTranslator;
	QtAppBootstrap::installTranslators(app, QStringLiteral("DeviceSelector"), qtTranslator, deviceSelectorTranslator);

	if (app.arguments().contains(QStringLiteral("--skin-shots")))
		return runSkinShots(app);

	if (app.arguments().contains(QStringLiteral("--diagnose")))
		return runDiagnose(app);

	if (app.arguments().contains(QStringLiteral("--install-endpoint")))
		return runEndpointCommand(app, true);

	if (app.arguments().contains(QStringLiteral("--uninstall-endpoint")))
		return runEndpointCommand(app, false);

	applyEditorTheme(app);

	if (app.arguments().contains("/u"))
	{
		const ApoRegistration::Result uninstallResult = ApoRegistration::uninstallAllDeviceApos(
			[](const std::wstring& message) {
				// /u is unattended: stderr cannot block on a modal dialog.
				fwprintf(stderr, L"DeviceSelector /u: %ls\n", message.c_str());
			});
		if (uninstallResult != ApoRegistration::Result::Success)
			result = -1;

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
