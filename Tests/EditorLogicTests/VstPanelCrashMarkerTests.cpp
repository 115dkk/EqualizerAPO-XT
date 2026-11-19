/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QSettings>
#include <QTemporaryDir>

#include "Editor/helpers/VstPanelCrashMarker.h"
#include "EditorLogicTestSupport.h"

TEST_CASE(EditorLogicTests, testVstPanelCrashMarkerWritesClearsAndConsumesLeftovers)
{
	QTemporaryDir temporary;
	requireTrue(temporary.isValid(), "temporary panel-marker settings directory exists");
	QSettings settings(temporary.filePath(QStringLiteral("panel-markers.ini")), QSettings::IniFormat);
	const QString first = QStringLiteral("C:/Plugins/First.dll");
	const QString second = QStringLiteral("C:/Plugins/Second.vst3");

	VstPanelCrashMarker::markOpen(settings, first);
	VstPanelCrashMarker::markOpen(settings, second);
	VstPanelCrashMarker::markClosed(settings, first);
	expectFalse(VstPanelCrashMarker::takeLeftover(settings, first),
		"a panel closed normally leaves no recovery marker");
	expectTrue(VstPanelCrashMarker::takeLeftover(settings, second),
		"an open panel is found after an abnormal exit");
	expectFalse(VstPanelCrashMarker::takeLeftover(settings, second),
		"the leftover panel marker is consumed once");
}

TEST_CASE(EditorLogicTests, testVstPanelCrashMarkerKeepsTwoLibrariesIndependent)
{
	QTemporaryDir temporary;
	requireTrue(temporary.isValid(), "temporary two-panel settings directory exists");
	QSettings settings(temporary.filePath(QStringLiteral("panel-markers.ini")), QSettings::IniFormat);
	const QString first = QStringLiteral("C:\\Plugins\\First.dll");
	const QString second = QStringLiteral("D:\\VST\\Second.dll");

	VstPanelCrashMarker::markOpen(settings, first);
	VstPanelCrashMarker::markOpen(settings, second);
	expectTrue(VstPanelCrashMarker::takeLeftover(settings, first),
		"the first plug-in has its own leftover marker");
	expectTrue(VstPanelCrashMarker::takeLeftover(settings, second),
		"consuming the first plug-in does not clear the second");
}

TEST_CASE(EditorLogicTests, testVstPanelCrashMarkerDiscardsCorruptSettings)
{
	QTemporaryDir temporary;
	requireTrue(temporary.isValid(), "temporary corrupt-marker settings directory exists");
	QSettings settings(temporary.filePath(QStringLiteral("panel-markers.ini")), QSettings::IniFormat);
	settings.setValue(QStringLiteral("session/openPluginPanels"), 42);
	settings.sync();

	expectFalse(VstPanelCrashMarker::takeLeftover(settings, QStringLiteral("C:/Plugins/First.dll")),
		"a corrupt marker value is not treated as an open panel");
	expectFalse(settings.contains(QStringLiteral("session/openPluginPanels")),
		"a corrupt marker value is removed");
}
