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
#include <QFile>
#include <QFontDatabase>
#include <QSettings>
#include <QStyleFactory>
#include <QtWidgets/QApplication>
#include <VoicemeeterAPOInfo.h>
#include <winsock2.h>
#include "ReceiveThread.h"
#include "DeviceSelector.h"
#include "Editor/helpers/QtAppBootstrap.h"
#include "Editor/skins/SkinThemeData.h"

namespace
{
// Dresses this dialog in the skin the user picked in the Editor (registry
// interface/skin + interface/dark; both default to the Editor's own defaults,
// so a machine that never chose gets Studio). Pure theme data - tokens, the
// QSS sheet, the palette and the bundled typefaces - via SkinThemeData; no
// Editor widget code is linked. In the Editor's heritage mode (legacyRows)
// the dialog keeps its classic native look, matching the Editor's own choice.
void applyEditorTheme(QApplication& app)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	if (settings.value(QStringLiteral("interface/legacyRows"), false).toBool())
	{
		if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
			app.setStyle(QStringLiteral("fusion"));
		return;
	}

	const bool systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
	const QString skinId = settings.value(QStringLiteral("interface/skin"), QStringLiteral("studio")).toString();
	const bool dark = settings.value(QStringLiteral("interface/dark"), systemDark).toBool();

	// The same typefaces the Editor bundles (static weights on purpose, see
	// Editor/main.cpp), so the sheets' font-family names resolve identically.
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

	app.setStyle(QStyleFactory::create(QStringLiteral("fusion")));
	const SkinTokens tokens = SkinThemeData::tokens(skinId, dark);
	app.setPalette(SkinThemeData::palette(tokens, dark));

	QString styleSheet;
	QFile sheet(SkinThemeData::qssResource(skinId, dark));
	if (sheet.open(QFile::ReadOnly))
		styleSheet = SkinThemeData::substituteTokens(QString::fromUtf8(sheet.readAll()), tokens);

	// The troubleshooting group reads as a disclosure, not a form checkbox:
	// its indicator becomes a fold chevron (the check state only ever meant
	// open/closed; see DeviceSelector::onTroubleShootingToggled).
	styleSheet += QStringLiteral(
		"#troubleshootingGroupBox::indicator { width: 12px; height: 12px; }"
		"#troubleshootingGroupBox::indicator:unchecked { image: url(:/icons/modern/chevron-right.svg); }"
		"#troubleshootingGroupBox::indicator:checked { image: url(:/icons/modern/chevron-down.svg); }");

	app.setStyleSheet(styleSheet + SkinThemeData::comboArrowOverride());
}
}

int main(int argc, char* argv[])
{
	int result = 0;

	// Shared bootstrap: anchors the plugin path (a security concern for this
	// elevated process) and, below, applies the language the user picked in
	// the Editor. (audit #146 TD011)
	QtAppBootstrap::addExecutableRelativePluginPath();

	QApplication app(argc, argv);
	applyEditorTheme(app);

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
