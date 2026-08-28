/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ConvolutionPathHelper.h"

#include <QDir>
#include <QFileInfo>

#include "filters/ConvolutionFilePath.h"

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
	const std::wstring resolved = ConvolutionFilePath::resolve(
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

	QDir configDir = configDirectory(configPath);
	QString relativePath = configDir.relativeFilePath(absolutePath);
	if (relativePathLooksContainedLexically(relativePath))
		return QDir::toNativeSeparators(relativePath);

	return QDir::toNativeSeparators(absolutePath);
}

// Lexical check only (QDir::cleanPath, no canonicalization or symlink resolution).
// Decides whether a selected path is stored relative in the user's config file.
// NOT a security boundary; the engine loads whatever path the config contains.
bool ConvolutionPathHelper::relativePathLooksContainedLexically(const QString& relativePath)
{
	QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
	return !cleanPath.isEmpty() && cleanPath != ".." && !cleanPath.startsWith("../") && !QDir::isAbsolutePath(cleanPath);
}
