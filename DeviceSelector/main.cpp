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
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSettings>
#include <QStyleFactory>
#include <QtWidgets/QApplication>
#include <VoicemeeterAPOInfo.h>
#include <winsock2.h>
#include "ReceiveThread.h"
#include "DeviceSelector.h"
#include "PreviewDevices.h"
#include "skins/DeviceSkinPainter.h"
#include "Editor/helpers/QtAppBootstrap.h"
#include "Editor/skins/SkinThemeData.h"

namespace
{
// The Editor's bundled typefaces (static weights on purpose, see
// Editor/main.cpp), so the sheets' font-family names resolve identically.
void addBundledFonts()
{
	static bool added = false;
	if (added)
		return;
	added = true;
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-Regular.ttf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-Medium.ttf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-SemiBold.ttf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-Bold.ttf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMMono-Regular.ttf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMMono-Medium.ttf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-Regular.otf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-SemiBold.otf"));
	QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-Bold.otf"));
	const QStringList cjkChain = {
		QStringLiteral("Pretendard"),
		QStringLiteral("Noto Sans KR"), QStringLiteral("Noto Sans"),
		QStringLiteral("Malgun Gothic"), QStringLiteral("Microsoft YaHei")
	};
	QFont::insertSubstitutions(QStringLiteral("DM Sans"), cjkChain);
	QFont::insertSubstitutions(QStringLiteral("DM Mono"),
		QStringList{ QStringLiteral("Consolas") } + cjkChain);
}

// Dresses the process in one skin: fusion base style, token palette, the
// skin's QSS colour coat, and the painter the custom-painted chrome (device
// rows, dialog buttons, disclosure header) resolves through.
void applyTheme(QApplication& app, const QString& skinId, bool dark)
{
	addBundledFonts();
	app.setStyle(QStyleFactory::create(QStringLiteral("fusion")));
	DeviceSkinPainter::setActiveTheme(skinId, dark);
	const SkinTokens tokens = SkinThemeData::tokens(skinId, dark);
	app.setPalette(SkinThemeData::palette(tokens, dark));

	QString styleSheet;
	QFile sheet(SkinThemeData::qssResource(skinId, dark));
	if (sheet.open(QFile::ReadOnly))
		styleSheet = SkinThemeData::substituteTokens(QString::fromUtf8(sheet.readAll()), tokens);

	app.setStyleSheet(styleSheet + SkinThemeData::comboArrowOverride());
}

// The skin the user picked in the Editor (registry interface/skin +
// interface/dark; both default to the Editor's own defaults, so a machine
// that never chose gets Studio). In the Editor's heritage mode (legacyRows)
// the dialog keeps its classic native look, matching the Editor's choice.
void applyEditorTheme(QApplication& app)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	if (settings.value(QStringLiteral("interface/legacyRows"), false).toBool())
	{
		// Neutral base forms in classic light colours for the painted chrome;
		// the stock sub-widgets keep the native style.
		DeviceSkinPainter::setHeritageTheme();
		if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
			app.setStyle(QStringLiteral("fusion"));
		return;
	}

	const bool systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
	applyTheme(app,
		settings.value(QStringLiteral("interface/skin"), QStringLiteral("studio")).toString(),
		settings.value(QStringLiteral("interface/dark"), systemDark).toBool());
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
	const QStringList skins = { QStringLiteral("studio"), QStringLiteral("minimal"),
		QStringLiteral("soft"), QStringLiteral("rack"), QStringLiteral("matrix") };
	for (const QString& skinId : skins)
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
			applyTheme(app, skinId, dark);

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
}

int main(int argc, char* argv[])
{
	int result = 0;

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

	applyEditorTheme(app);

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
