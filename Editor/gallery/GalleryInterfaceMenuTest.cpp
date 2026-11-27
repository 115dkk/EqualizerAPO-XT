/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "Editor/SkinGallery.h"
#include "Editor/MainWindow.h"
#include "Editor/EditorGates.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/skins/SkinThemeData.h"
#include "Editor/widgets/FilterCardRow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

namespace
{
QVariantMap preferencesSnapshot()
{
	const QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QVariantMap result;
	for (const QString& key : settings.allKeys())
		result.insert(key, settings.value(key));
	return result;
}

QAction* skinEntry(const QMenu* menu, const QString& skin)
{
	if (menu != nullptr)
		for (QAction* action : menu->actions())
			if (action->data().toMap().value(QStringLiteral("skin")).toString() == skin)
				return action;
	return nullptr;
}
}

int SkinGallery::runInterfaceMenuTest()
{
	QTemporaryDir scratch;
	if (!scratch.isValid() || QApplication::platformName() != QLatin1String("offscreen"))
	{
		qWarning("InterfaceMenuTest: FAIL (requires offscreen and a temporary directory)");
		return 1;
	}
	QSettings testSettings(scratch.filePath(QStringLiteral("editor-test.ini")), QSettings::IniFormat);
	EditorSettings::writeListMode(testSettings, EditorSettings::ListMode::LegacyEditor);
	testSettings.sync();
	const QVariantMap savedPreferences = preferencesSnapshot();
	qApp->setQuitOnLastWindowClosed(false);
	SkinManager::instance()->applySkin(QStringLiteral("studio"), false);
	int failures = 0;
	const auto check = [&failures](bool condition, const char* message) {
		if (!condition)
		{
			qWarning("InterfaceMenuTest: %s", message);
			failures++;
		}
	};
	{
		MainWindow window(QDir(scratch.path()), nullptr, nullptr, true);
		const QString path = scratch.filePath(QStringLiteral("menu.txt"));
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write("Preamp: -3 dB\r\nDelay: 1 ms\r\n") < 0)
		{
			qWarning("InterfaceMenuTest: FAIL (cannot write configuration)");
			return 1;
		}
		file.close();
		window.probe().instantModeControl()->setChecked(false);
		window.load(path);
		window.show();
		QApplication::processEvents();
		FilterTable* table = window.probe().currentFilterTable();
		check(table != nullptr, "open document has no table");
		const auto requestedMode = EditorGates::headlessListMode() == EditorSettings::ListMode::Rows
			? FilterTable::ModernRows : FilterTable::ModernCards;
		check(table != nullptr && table->getRenderMode() == requestedMode,
			"headless window restored the saved presentation instead of the requested/default mode");

		QMenu* interfaceMenu = nullptr;
		for (QMenu* menu : window.findChildren<QMenu*>())
			if (menu->title() == QLatin1String("Interface"))
				interfaceMenu = menu;
		check(interfaceMenu != nullptr, "Interface menu is missing");
		const QList<QAction*> entries = interfaceMenu != nullptr ? interfaceMenu->actions() : QList<QAction*>();
		check(entries.size() >= 5, "Interface menu has fewer than five entries");
		if (entries.size() >= 5)
		{
			QMenu* cards = entries[0]->menu();
			QMenu* rows = entries[1]->menu();
			QAction* legacy = entries[2];
			check(entries[0]->text() == QLatin1String("Modern cards") && cards != nullptr,
				"first entry must be the Modern cards submenu");
			check(entries[1]->text() == QLatin1String("Modern rows") && rows != nullptr,
				"second entry must be the Modern rows submenu");
			check(legacy->text() == QLatin1String("Legacy editor") && legacy->menu() == nullptr,
				"third entry must be Legacy editor");
			check(entries[3]->isSeparator(), "fourth entry must be a separator");
			check(entries[4]->text() == QLatin1String("Dark theme"), "fifth entry must be Dark theme");
			check(entries[4]->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+D")), "dark shortcut changed");
			check(entries.size() > 6 && entries[5]->isSeparator()
				&& entries[6]->text() == QLatin1String("Knob gain range"), "entries after Dark theme changed");

			if (cards != nullptr && rows != nullptr && table != nullptr)
			{
				const QString originalSkin = SkinManager::instance()->currentSkinId();
				const bool originalDark = SkinManager::instance()->isDark();
				QStringList rowSkinIds;
				for (ISkin* skin : Skins::all())
				{
					check(SkinManager::instance()->skinSupportsRows(skin->id()) == skin->supportsRowMode(),
						"row capability lookup disagrees with the registered skin");
					if (skin->supportsRowMode())
						rowSkinIds.append(skin->id());
				}
				check(originalSkin == SkinManager::instance()->currentSkinId()
					&& originalDark == SkinManager::instance()->isDark(), "capability lookup applied a skin");
				for (QMenu* menu : { cards, rows })
				{
					QStringList ids;
					for (QAction* action : menu->actions())
					{
						const QVariantMap choice = action->data().toMap();
						ids.append(choice.value(QStringLiteral("skin")).toString());
						check(choice.value(QStringLiteral("mode")).toInt() == static_cast<int>(
							menu == cards ? FilterTable::ModernCards : FilterTable::ModernRows), "entry has the wrong presentation");
						check(action->isCheckable() && action->shortcuts().isEmpty(), "menu entry owns a shortcut or cannot be checked");
						check(action->actionGroup() == legacy->actionGroup(), "entries do not share one group");
					}
					check(ids == (menu == cards ? SkinThemeData::ids() : rowSkinIds), "submenu skin roster or order is wrong");
					check(menu->menuAction()->isCheckable() && menu->menuAction()->actionGroup() == nullptr,
						"parent entry must be checkable outside the exclusive group");
				}
				QActionGroup* group = legacy->actionGroup();
				check(group != nullptr && group->isExclusive()
					&& group->actions().size() == cards->actions().size() + rows->actions().size() + 1,
					"exclusive group has the wrong membership");

				const auto checkedChoice = [&](QAction* chosen, FilterTable::RenderMode mode) {
					int checked = 0;
					if (group != nullptr)
						for (const QAction* action : group->actions())
							checked += action->isChecked() ? 1 : 0;
					check(chosen != nullptr && chosen->isChecked() && checked == 1
						&& group != nullptr && group->checkedAction() == chosen, "checked leaf is not the sole current choice");
					check(cards->menuAction()->isChecked() == (mode == FilterTable::ModernCards)
						&& rows->menuAction()->isChecked() == (mode == FilterTable::ModernRows), "parent checks do not match the presentation");
					for (QMenu* menu : { cards, rows })
						for (const QAction* action : menu->actions())
							check(action->text().contains(QLatin1Char('\t')) ==
								(mode == (menu == cards ? FilterTable::ModernCards : FilterTable::ModernRows)),
								"shortcut text appears in the wrong submenu");
				};
				int prompts = 0;
				QMessageBox::StandardButton restartAnswer = QMessageBox::No;
				QMessageBox::StandardButton saveAnswer = QMessageBox::Cancel;
				// The close-save gate answers modal questions with a timer too.
				// Unexpected questions are declined instead of hanging a headless run.
				QTimer answer;
				answer.setInterval(5);
				QObject::connect(&answer, &QTimer::timeout, [&]() {
					QMessageBox* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
					if (box == nullptr)
						return;
					prompts++;
					const auto response = box->standardButtons().testFlag(QMessageBox::Save) ? saveAnswer : restartAnswer;
					if (QAbstractButton* button = box->button(response))
						button->click();
					else
						box->reject();
				});
				answer.start();
				QAction* studio = skinEntry(cards, QStringLiteral("studio"));
				QAction* rack = skinEntry(rows, QStringLiteral("rack"));
				check(studio != nullptr && rack != nullptr, "required studio/cards and rack/rows entries are missing");
				if (studio != nullptr && rack != nullptr)
				{
					studio->trigger();
					checkedChoice(studio, FilterTable::ModernCards);
					QPointer<FilterCardRow> oldRow = table->findChild<FilterCardRow*>();
					rack->trigger();
					check(prompts == 0 && !window.shouldRestart(), "modern switch requested a restart");
					check(SkinManager::instance()->currentSkinId() == QLatin1String("rack")
						&& table->getRenderMode() == FilterTable::ModernRows
						&& table->effectiveRenderMode() == FilterTable::ModernRows, "rack selection did not build modern rows");
					check(oldRow.isNull(), "modern switch left the previous row alive");
					checkedChoice(rack, FilterTable::ModernRows);
					rows->menuAction()->trigger();
					checkedChoice(rack, FilterTable::ModernRows);
					cards->popup(window.mapToGlobal(QPoint(10, 10)));
					QApplication::processEvents();
					checkedChoice(rack, FilterTable::ModernRows);
					cards->hide();
					studio->trigger();
					check(prompts == 0 && !window.shouldRestart()
						&& SkinManager::instance()->currentSkinId() == QLatin1String("studio")
						&& table->effectiveRenderMode() == FilterTable::ModernCards, "studio selection did not return to cards live");
					checkedChoice(studio, FilterTable::ModernCards);

					const QStringList ids = SkinThemeData::ids();
					for (int i = 0; i < ids.size(); i++)
					{
						const QKeySequence key(QStringLiteral("Ctrl+Alt+%1").arg(i + 1));
						QList<QAction*> holders;
						for (QAction* action : window.findChildren<QAction*>())
							if (action->shortcuts().contains(key))
								holders.append(action);
						check(holders.size() == 1, "skin shortcut is missing or ambiguous");
						if (holders.size() != 1)
							continue;
						QAction* shortcut = holders.first();
						check(window.actions().contains(shortcut) && shortcut->shortcutContext() == Qt::WindowShortcut
							&& shortcut->actionGroup() == nullptr && shortcut->data().toString() == ids[i],
							"shortcut is not the dedicated window action");
						rack->trigger();
						shortcut->trigger();
						const bool supportsRows = SkinManager::instance()->skinSupportsRows(ids[i]);
						const auto mode = supportsRows ? FilterTable::ModernRows : FilterTable::ModernCards;
						check(table->getRenderMode() == mode && table->effectiveRenderMode() == mode
							&& SkinManager::instance()->currentSkinId() == ids[i], "shortcut did not keep rows or fall back to cards");
						checkedChoice(skinEntry(supportsRows ? rows : cards, ids[i]), mode);
					}
					check(prompts == 0, "a shortcut requested a restart in a modern presentation");

					studio->trigger();
					legacy->trigger();
					check(prompts == 1 && !window.shouldRestart(), "declining legacy did not cancel the restart");
					checkedChoice(studio, FilterTable::ModernCards);
					restartAnswer = QMessageBox::Yes;
					table->setLineText(table->documentItems().first(), QStringLiteral("Preamp: -4 dB"));
					legacy->trigger();
					check(prompts == 3 && !window.shouldRestart(), "cancelled close did not cancel the accepted restart");
					checkedChoice(studio, FilterTable::ModernCards);
					saveAnswer = QMessageBox::Discard;
					legacy->trigger();
					check(prompts == 5 && window.shouldRestart(), "accepting legacy did not schedule a restart");
					checkedChoice(legacy, FilterTable::LegacyEditor);
					check(cards->isEnabled() && rows->isEnabled() && !entries[4]->isEnabled(), "legacy disabled submenus or enabled Dark theme");
					for (QAction* action : window.actions())
						if (action->objectName().startsWith(QLatin1String("SkinShortcut_")))
							action->trigger();
					checkedChoice(legacy, FilterTable::LegacyEditor);
					check(prompts == 5, "a skin shortcut acted in the legacy editor");
					restartAnswer = QMessageBox::No;
					rack->trigger();
					check(prompts == 6, "leaving legacy did not ask for a restart");
					checkedChoice(legacy, FilterTable::LegacyEditor);
					restartAnswer = QMessageBox::Yes;
					rack->trigger();
					check(prompts >= 7 && window.shouldRestart(), "accepting rows from legacy did not schedule a restart");
					checkedChoice(rack, FilterTable::ModernRows);
				}
				answer.stop();
			}
		}
	}
	testSettings.sync();
	check(EditorSettings::readListMode(testSettings) == EditorSettings::ListMode::LegacyEditor,
		"headless menu test persisted a list-mode choice");
	check(savedPreferences == preferencesSnapshot(), "headless menu test changed registry preferences");
	qWarning("InterfaceMenuTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
