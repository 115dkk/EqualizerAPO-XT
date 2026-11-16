/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <optional>

#include <QStringList>

class MainWindow;
class QCommandLineParser;

namespace EditorGates
{
std::optional<int> runHeadless(const QStringList& arguments);
bool wantsTestWindow(const QStringList& arguments);
void registerOptions(QCommandLineParser& parser);
bool arm(MainWindow& window, const QCommandLineParser& parser);
QStringList flags();
}
