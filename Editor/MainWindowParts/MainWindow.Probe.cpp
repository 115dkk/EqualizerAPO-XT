/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "Editor/MainWindowProbe.h"

#include <utility>

MainWindow::Probe MainWindow::probe()
{
	return Probe(*this);
}

MainWindow::Probe::Probe(MainWindow& owner)
	: mainWindow(owner)
{
}

void MainWindow::Probe::suppressPreferenceWrites()
{
	mainWindow.noSavePreferences = true;
	mainWindow.noSaveFilePreferences = true;
	// The skin choice is a preference too; probes that walk the skin menu
	// used to leave the last skin they visited in the registry.
	mainWindow.skinPersistenceSuppressed = true;
}

FilterTable* MainWindow::Probe::currentFilterTable() const
{
	return mainWindow.currentFilterTable();
}

QCheckBox* MainWindow::Probe::instantModeControl() const
{
	return mainWindow.instantModeCheckBox;
}

QComboBox* MainWindow::Probe::deviceControl() const
{
	return mainWindow.deviceComboBox;
}

void MainWindow::Probe::onSaveStarted(std::function<void()> hook)
{
	mainWindow.analysisProbeSaveStarted = std::move(hook);
}

void MainWindow::Probe::onAnalysisParametersSet(std::function<void()> hook)
{
	mainWindow.analysisProbeParametersSet = std::move(hook);
}

void MainWindow::Probe::onAnalysisPanelUpdated(std::function<void(double, double, double)> hook)
{
	mainWindow.analysisProbePanelUpdated = std::move(hook);
}

void MainWindow::Probe::startSkinSwitchStorm()
{
	mainWindow.startSkinSwitchStorm();
}
