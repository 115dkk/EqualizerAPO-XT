/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <sstream>
#include "services/registry/RegistryPaths.h"
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QScrollArea>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "services/settings/EditorSettings.h"
#include "version.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "SkinManager.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


void MainWindow::on_mainToolBar_visibilityChanged(bool visible)
{
	ui->actionToolbar->setChecked(visible);
}


void MainWindow::on_analysisDockWidget_visibilityChanged(bool visible)
{
	ui->actionAnalysisPanel->setChecked(visible);

	if (visible)
		startAnalysis();
}

void MainWindow::on_actionToolbar_triggered(bool checked)
{
	ui->mainToolBar->setVisible(checked);
}

void MainWindow::on_actionAnalysisPanel_triggered(bool checked)
{
	ui->analysisDockWidget->setVisible(checked);
}

void MainWindow::interfaceModeSelected(QAction* action)
{
	if (action == nullptr)
		return;

	const QVariantMap choice = action->data().toMap();
	selectInterface(static_cast<FilterTable::RenderMode>(choice.value(QStringLiteral("mode")).toInt()),
		choice.value(QStringLiteral("skin"), skinId).toString());
}

void MainWindow::selectInterface(FilterTable::RenderMode mode, const QString& selectedSkin)
{
	if (mode == currentRenderMode && selectedSkin == skinId)
	{
		syncInterfaceMenu();
		persistSkinChoice();
		return;
	}

	// The legacy editor runs unskinned on the native style, frame, font engine
	// and system fonts. None of that can swap cleanly inside a live process,
	// and a partial swap mixes modern chrome around legacy rows. Restart only
	// when entering or leaving that presentation.
	if ((mode == FilterTable::LegacyEditor) != (currentRenderMode == FilterTable::LegacyEditor))
	{
		if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
		{
			const auto previousMode = currentRenderMode;
			const QString previousSkin = skinId;
			const bool previousRestart = restart;
			// Close persists both values, so the next Editor opens with exactly
			// the submenu choice. Leave the old rows intact to commit pending edits.
			currentRenderMode = mode;
			skinId = selectedSkin;
			restart = true;
			if (close())
				persistSkinChoice();
			else
			{
				// Cancelling an unsaved-document prompt cancels the restart too.
				currentRenderMode = previousMode;
				skinId = previousSkin;
				restart = previousRestart;
			}
		}
		syncInterfaceMenu();
		return;
	}

	currentRenderMode = mode;
	skinId = selectedSkin;
	applySkinAndRebuild();
}

void MainWindow::skinSelected(QAction* action)
{
	if (action == nullptr || currentRenderMode == FilterTable::LegacyEditor)
		return;

	const QString selectedSkin = action->data().toString();
	const auto mode = currentRenderMode == FilterTable::ModernRows
		&& SkinManager::instance()->skinSupportsRows(selectedSkin)
		? FilterTable::ModernRows : FilterTable::ModernCards;
	selectInterface(mode, selectedSkin);
}

void MainWindow::applySkinAndRebuild()
{
	// Tear the rows down BEFORE swapping the global stylesheet. qApp->setStyleSheet
	// re-polishes every live widget, and a loaded config inflates the tree to
	// thousands of widgets; re-polishing them only to immediately rebuild them in
	// updateGuis() below made each skin switch cost seconds. Clearing first lets
	// the stylesheet apply against just the window chrome, and the rebuilt rows
	// are polished a single time when they are created.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->clearRows();
	}
	SkinManager::instance()->applySkin(skinId, skinDark);
	skinId = SkinManager::instance()->currentSkinId();
	// The new sheet re-polished the analysis bar; re-pin the dock floor so a
	// graph parked at the floor keeps its size across the switch.
	updateAnalysisDockFloor();
	// Each skin supplies its own Copy routing renderer (node graph, crosspoint
	// matrix, step list, ...) and per-skin card chrome. Those widgets are built
	// once when the row is created, so the rows must be rebuilt for the new
	// skin to take effect — a plain repaint only re-colours the old widgets and
	// left every skin showing the studio node graph for Copy.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
		{
			// setRenderMode rebuilds when the mode changes. Do not also call
			// updateGuis in that case: both choices commit in this single pass.
			if (filterTable->getRenderMode() != currentRenderMode)
				filterTable->setRenderMode(currentRenderMode);
			else
				filterTable->updateGuis();
		}
	}
	syncInterfaceMenu();
	persistSkinChoice();
}

void MainWindow::darkThemeToggled(bool checked)
{
	skinDark = checked;
	applySkinAndRebuild();
}

void MainWindow::on_graphPositionComboBox_currentIndexChanged(int index)
{
	if (index < 0 || index > 2 || index == graphDockPosition)
		return;

	graphDockPosition = index;
	applyRedesignPreferences();
}

void MainWindow::knobRangeSelected(QAction* action)
{
	if (action == nullptr)
		return;

	double range = action->data().toDouble();
	if (range == 0.0)
	{
		// The "Custom..." entry asks for a number instead of carrying one.
		bool ok = false;
		range = QInputDialog::getDouble(this, tr("Knob gain range"),
			tr("Gain knobs will cover ± this many dB:"),
			GUIHelper::knobGainRange(), 1.0, 100.0, 1, &ok);
		if (!ok)
		{
			syncKnobRangeActions();
			return;
		}
	}

	GUIHelper::setKnobGainRange(range);
	syncKnobRangeActions();
	// Rebuild the open rows so existing Preamp / Filter knobs pick up the new span.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->updateGuis();
	}
}

void MainWindow::toggleGraphFullscreen()
{
	graphFullscreen = !graphFullscreen;
	if (graphFullscreen)
	{
		// Remember the toolbar's visibility BEFORE hiding it: the hide fires
		// visibilityChanged, which unchecks actionToolbar, so consulting the
		// action on the way out latched the toolbar hidden forever after one
		// fullscreen round trip - the "toolbar is gone" field report.
		toolbarVisibleBeforeGraphFullscreen = !ui->mainToolBar->isHidden();
		ui->centralWidget->setVisible(false);
		ui->mainToolBar->setVisible(false);
	}
	else
	{
		ui->centralWidget->setVisible(true);
		ui->mainToolBar->setVisible(toolbarVisibleBeforeGraphFullscreen);
	}
	ui->analysisDockWidget->setVisible(true);
	if (fullscreenGraphAction != nullptr)
		fullscreenGraphAction->setChecked(graphFullscreen);
}

void MainWindow::nativeTitleBarToggled(bool checked)
{
	QAction* action = qobject_cast<QAction*>(sender());

	// Frame styles can only change cleanly before the window exists, so this
	// follows the language flow: persist the choice and offer a restart.
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		settings.setValue(QLatin1String(EditorSettings::Keys::NativeTitleBar), checked);
		restart = true;
		close();
	}
	else if (action != nullptr)
	{
		action->blockSignals(true);
		action->setChecked(!checked);
		action->blockSignals(false);
	}
}

void MainWindow::languageSelected(bool selected)
{
	QAction* action = qobject_cast<QAction*>(sender());

	if (!selected)
	{
		action->setChecked(true);
		return;
	}

	QLocale::Language language = static_cast<QLocale::Language>(action->data().toInt());

	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		if (language == QLocale::AnyLanguage)
		{
			settings.remove(QLatin1String(EditorSettings::Keys::Language));
		}
		else
		{
			QString name = QLocale(language).name();
			int index = name.indexOf('_');
			if (index != -1)
				name = name.left(index);
			settings.setValue(QLatin1String(EditorSettings::Keys::Language), name);
		}

		restart = true;
		close();
	}
	else
	{
		action->setChecked(false);
	}
}

void MainWindow::on_actionResetAllGlobalPreferences_triggered()
{
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		for (const QString& key : settings.childGroups())
		{
			if (key != "file-specific")
				settings.remove(key);
		}
		for (const QString& key : settings.childKeys())
			settings.remove(key);

		// Closing may be cancelled by an unsaved document. Refresh this action
		// now as well as restoring its default in the next window.
		ui->actionAutomaticUpdates->blockSignals(true);
		ui->actionAutomaticUpdates->setChecked(EditorSettings::readAutomaticUpdates(settings));
		ui->actionAutomaticUpdates->blockSignals(false);
		restart = true;
		noSavePreferences = true;
		close();
	}
}

void MainWindow::on_actionResetAllFileSpecificPreferences_triggered()
{
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
		for (const QString& key : settings.childGroups())
			settings.remove(key);
		for (const QString& key : settings.childKeys())
			settings.remove(key);

		restart = true;
		noSaveFilePreferences = true;
		close();
	}
}

