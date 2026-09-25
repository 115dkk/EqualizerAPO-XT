/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FileReferenceController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "filters/ConfigFileReference.h"
#include "filters/ConfigPathPolicy.h"
#include "services/security/AudioEngineAccess.h"

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
	resolved = QDir::toNativeSeparators(QString::fromStdWString(ConfigFileReference::resolve(
		QDir::toNativeSeparators(configPath).toStdWString(), written.toStdWString())));
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

	// Named the way the engine reads the text: without its quotes, with its
	// variables expanded.
	const QString normalized = QDir::fromNativeSeparators(
		QString::fromStdWString(ConfigFileReference::normalize(written.toStdWString())));
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

QString FileReferenceController::audioServiceProblem(const QString& absolutePath, const QString& configPath)
{
	// The offscreen gallery renders synthetic files with no meaningful ACL or
	// location story.
	if (absolutePath.isEmpty() || qEnvironmentVariableIsSet("EAPO_SKIN_GALLERY"))
		return {};

	const std::wstring path = QDir::toNativeSeparators(absolutePath).toStdWString();
	std::wstring reason;
	if (!ConfigPathPolicy::allowsOpen(path, QDir::toNativeSeparators(configPath).toStdWString(), reason))
		return QCoreApplication::translate("FileReferenceController", "The audio service only opens files on local drives");
	if (!AudioEngineAccess::isReadableByAudioEngine(path))
		return QCoreApplication::translate("FileReferenceController", "Not readable by the audio service");
	return {};
}
