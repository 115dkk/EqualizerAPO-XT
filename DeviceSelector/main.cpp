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
#include <devices/DeviceAPOInfo.h>
#include <helpers/RegistryHelper.h>
#include <ObjBase.h>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QFontDatabase>
#include <QSettings>
#include <QStyleFactory>
#include <QtWidgets/QApplication>
#include <devices/VoicemeeterAPOInfo.h>
#include <winsock2.h>
#include "ReceiveThread.h"
#include "DeviceSelector.h"
#include "PreviewDevices.h"
#include "skins/DeviceSkinPainter.h"
#include "Editor/helpers/QtAppBootstrap.h"
#include "Editor/helpers/EditorSettings.h"
#include "Editor/skins/SkinThemeData.h"
#include "helpers/ApoRegistration.h"
#include "helpers/InstallDiagnostics.h"
#include "helpers/LogHelper.h"

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
}

int main(int argc, char* argv[])
{
	int result = 0;

	// Before anything else, because this is the process that performs the APO
	// install and until now it wrote no log at all. A user reporting that
	// installing did nothing left behind an Editor.log with no mention of the
	// install, because the Editor was not the program that ran it.
	if (!LogHelper::useUserFile(L"DeviceSelector.log", true, false, false))
		LogHelper::useDefaultApoLog();

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
