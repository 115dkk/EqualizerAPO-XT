/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ConvolutionPathHelper.h"

#include <QDir>
#include <QFileInfo>

#include "Editor/widgets/cards/FileReferenceController.h"
#include "filters/ConfigFileReference.h"

namespace
{
QDir configDirectory(const QString& configPath)
{
	QFileInfo fileInfo(configPath);
	return fileInfo.absoluteDir();
}
}

QString ConvolutionPathHelper::absolutePathForConfig(const QString& configPath, const QString& path)
{
	const std::wstring resolved = ConfigFileReference::resolve(
		configPath.toStdWString(), path.toStdWString());
	if (resolved.empty())
		return QString();
	return QDir::cleanPath(QString::fromStdWString(resolved));
}

QString ConvolutionPathHelper::displayPathForSelection(const QString& configPath, const QString& selectedPath)
{
	QString absolutePath = absolutePathForConfig(configPath, selectedPath);
	if (absolutePath.isEmpty())
		return QString();

	// The cards' rule, so a legacy row writes a chosen file the way a card does.
	return FileReferenceController::displayPathForBaseDirectory(
		configDirectory(configPath).absolutePath(), absolutePath);
}
