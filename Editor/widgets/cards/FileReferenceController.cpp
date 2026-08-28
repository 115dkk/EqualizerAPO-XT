/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FileReferenceController.h"

#include <QDir>
#include <QFileInfo>

FileReferenceController::FileReferenceController(const QString& kind,
	const QString& writtenPath, QObject* parent)
	: QObject(parent), referenceKind(kind), written(writtenPath.trimmed())
{
}

const QString& FileReferenceController::writtenPath() const
{
	return written;
}

const QString& FileReferenceController::resolvedPath() const
{
	return resolved;
}

void FileReferenceController::setWrittenPath(const QString& path)
{
	written = path.trimmed();
}

void FileReferenceController::setResolvedPath(const QString& path)
{
	resolved = QDir::toNativeSeparators(path);
}

void FileReferenceController::resolveAgainstConfig(const QString& configPath)
{
	resolveAgainstDirectory(QFileInfo(configPath).absolutePath());
}

void FileReferenceController::resolveAgainstDirectory(const QString& directoryPath)
{
	if (written.isEmpty())
	{
		resolved.clear();
		return;
	}
	const QString normalized = QDir::fromNativeSeparators(written);
	resolved = QDir::toNativeSeparators(
		QDir::isAbsolutePath(normalized)
			? QFileInfo(normalized).absoluteFilePath()
			: QDir(directoryPath).absoluteFilePath(normalized));
}

QString FileReferenceController::displayPathForBaseDirectory(
	const QString& baseDirectory, const QString& selectedPath)
{
	QString relative = QDir(baseDirectory).relativeFilePath(selectedPath);
	if (relative.startsWith(QStringLiteral("../../")))
		relative = selectedPath;
	return QDir::toNativeSeparators(relative);
}

ReferenceCardState FileReferenceController::describe(const QString& emptyName) const
{
	ReferenceCardState state;
	state.kind = referenceKind;
	state.editText = written;
	if (written.isEmpty())
	{
		state.missing = true;
		state.name = emptyName;
		return state;
	}

	const QString normalized = QDir::fromNativeSeparators(written);
	const QFileInfo asWritten(normalized);
	state.name = asWritten.fileName();
	state.absolutePath = QDir::isAbsolutePath(normalized);
	const QFileInfo resolvedInfo(resolved);
	state.missing = resolved.isEmpty() || !resolvedInfo.exists();
	if (!state.missing)
		state.fullPath = QDir::toNativeSeparators(resolvedInfo.absoluteFilePath());
	if (state.absolutePath && !resolved.isEmpty())
		state.directory = QDir::toNativeSeparators(resolvedInfo.absolutePath());
	else if (asWritten.path() != QStringLiteral("."))
		state.directory = QDir::toNativeSeparators(asWritten.path());
	return state;
}
