/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VstPanelCrashMarker.h"

#include <QDir>
#include <QMetaType>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include "services/registry/RegistryPaths.h"

namespace
{
constexpr auto markerKey = "session/openPluginPanels";

QStringList readMarkers(QSettings& settings)
{
	const QVariant stored = settings.value(QLatin1String(markerKey));
	if (!stored.isValid())
		return {};
	if (stored.metaType() != QMetaType::fromType<QStringList>())
	{
		settings.remove(QLatin1String(markerKey));
		settings.sync();
		return {};
	}
	return stored.toStringList();
}

void writeMarkers(QSettings& settings, const QStringList& markers)
{
	if (markers.isEmpty())
		settings.remove(QLatin1String(markerKey));
	else
		settings.setValue(QLatin1String(markerKey), markers);
	settings.sync();
}
}

QString VstPanelCrashMarker::normalizedIdentity(const QString& libraryPath)
{
	return QDir::cleanPath(QDir::fromNativeSeparators(libraryPath)).toCaseFolded();
}

void VstPanelCrashMarker::markOpen(const QString& libraryPath)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	markOpen(settings, libraryPath);
}

void VstPanelCrashMarker::markClosed(const QString& libraryPath)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	markClosed(settings, libraryPath);
}

bool VstPanelCrashMarker::takeLeftover(const QString& libraryPath)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	return takeLeftover(settings, libraryPath);
}

void VstPanelCrashMarker::markOpen(QSettings& settings, const QString& libraryPath)
{
	// Adapted from Custom Audio Manager by weirdsoup (MIT); see ThirdPartyNotices.txt.
	const QString identity = normalizedIdentity(libraryPath);
	if (identity.isEmpty() || identity == QStringLiteral("."))
		return;
	QStringList markers = readMarkers(settings);
	if (!markers.contains(identity))
		markers.append(identity);
	writeMarkers(settings, markers);
}

void VstPanelCrashMarker::markClosed(QSettings& settings, const QString& libraryPath)
{
	const QString identity = normalizedIdentity(libraryPath);
	QStringList markers = readMarkers(settings);
	markers.removeAll(identity);
	writeMarkers(settings, markers);
}

bool VstPanelCrashMarker::takeLeftover(QSettings& settings, const QString& libraryPath)
{
	const QString identity = normalizedIdentity(libraryPath);
	QStringList markers = readMarkers(settings);
	const bool found = markers.removeAll(identity) != 0;
	writeMarkers(settings, markers);
	return found;
}
