/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <optional>

#include <QStringList>

#include "services/settings/EditorSettings.h"

class MainWindow;
class QCommandLineParser;

namespace EditorGates
{
std::optional<int> runHeadless(const QStringList& arguments);
// Engaged only while a headless runner is active; cards when no modifier was
// given. A headless window must never restore the user's saved presentation.
std::optional<EditorSettings::ListMode> headlessListMode();
// The skin a headless check opens its Editor window in, when the run names
// one with --window-skin <id> (its light finish). Without it the window keeps
// the skin this machine has saved, and a runner has none saved: there the
// window opens in the default skin. A check that walks every skin itself
// never asks, and a run that names a skin nobody asked for fails.
std::optional<QString> headlessWindowSkin();
// Record which presentation a skin got in this run. With rows requested, a
// skin without them is logged once as falling back to cards, and a run in
// which no skin drew rows fails: it would pass as row coverage otherwise.
void noteListModeSkin(const QString& skinId);
bool wantsTestWindow(const QStringList& arguments);
void registerOptions(QCommandLineParser& parser);
bool arm(MainWindow& window, const QCommandLineParser& parser);
QStringList flags();
}
