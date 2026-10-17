/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What the offscreen probes (Editor/gallery) and the diagnostic sessions
	(Editor/diagnostics) may reach inside the live MainWindow beyond its
	public interface. They used to be friends with access to all of it
	(audit #422 TD-59); this names the few things they actually use. A
	nested class reaches MainWindow's private members without a friend
	declaration.
*/

#pragma once

#include <functional>

#include "Editor/MainWindow.h"

class QCheckBox;
class QComboBox;
class FilterTable;

class MainWindow::Probe
{
public:
	explicit Probe(MainWindow& owner);

	// A diagnostic session must not overwrite the user's window and per-file
	// preferences, even when it ends early.
	void suppressPreferenceWrites();
	FilterTable* currentFilterTable() const;
	QCheckBox* instantModeControl() const;
	QComboBox* deviceControl() const;
	// Timing hooks on the save -> analysis path: a save started, the
	// analysis worker was handed its parameters, the panel showed a result
	// (initialization, processing and panel time in milliseconds).
	void onSaveStarted(std::function<void()> hook);
	void onAnalysisParametersSet(std::function<void()> hook);
	void onAnalysisPanelUpdated(std::function<void(double, double, double)> hook);
	// The hidden skin-switch storm (Editor/diagnostics/SkinSwitchStorm.cpp).
	void startSkinSwitchStorm();

private:
	MainWindow& mainWindow;
};
