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

void FileReferenceController::attach(std::function<QString()> configPath, std::function<void()> committed)
{
	configPathSource = std::move(configPath);
	onCommitted = std::move(committed);
}

QString FileReferenceController::resolvedAbsolutePath()
{
	if (!configPathSource)
		return QString();

	resolveAgainstConfig(configPathSource());
	return resolved;
}

void FileReferenceController::commitWrittenPath(const QString& text)
{
	setWrittenPath(text);
	if (onCommitted)
		onCommitted();
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
	// The user typed or chose the reference: ask again whether it exists,
	// even when the text is the same (the file may be there now).
	presence.forget();
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

QString FileReferenceController::displayPathForLibrary(const QString& libraryPath, const QString& pluginsDirectory)
{
	const QString relative = QDir::toNativeSeparators(QDir(pluginsDirectory).relativeFilePath(libraryPath));
	if (relative.startsWith(QDir::toNativeSeparators(QStringLiteral("../../"))))
		return libraryPath;
	return relative;
}

QString FileReferenceController::absolutePathForConfig(const QString& configPath, const QString& path)
{
	const std::wstring resolvedPath = ConfigFileReference::resolve(
		configPath.toStdWString(), path.toStdWString());
	if (resolvedPath.empty())
		return QString();
	return QDir::cleanPath(QString::fromStdWString(resolvedPath));
}

QString FileReferenceController::displayPathForConfigSelection(const QString& configPath, const QString& selectedPath)
{
	const QString absolutePath = absolutePathForConfig(configPath, selectedPath);
	if (absolutePath.isEmpty())
		return QString();

	// The cards' rule, so a legacy row writes a chosen file the way a card does.
	return displayPathForBaseDirectory(QFileInfo(configPath).absoluteDir().absolutePath(), absolutePath);
}

void FileReferenceController::adoptChosenFile(const QString& selectedPath, const QString& referenceBaseDirectory)
{
	written = referenceBaseDirectory.isEmpty()
		? QDir::toNativeSeparators(selectedPath)
		: displayPathForBaseDirectory(referenceBaseDirectory, selectedPath);
	resolved = QDir::toNativeSeparators(selectedPath);
	presence.forget();
}

QString FileReferenceController::presenceKey() const
{
	return written + QLatin1Char('\n') + resolved;
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
	if (resolved.isEmpty())
		state.missing = true;
	else if (!probesPresenceInBackground())
		state.missing = !resolvedInfo.exists();
	else
	{
		// No file-system call here: QFileInfo::exists on a disconnected
		// network drive blocked the UI thread for the network timeout each
		// time a row was built (audit #467 TD-59). askPresence asks.
		switch (presence.answerFor(presenceKey()))
		{
		case ReferencePresence::Answer::Unknown:
			state.checking = true;
			break;
		case ReferencePresence::Answer::Missing:
			state.missing = true;
			break;
		case ReferencePresence::Answer::Present:
			break;
		}
	}
	if (!state.missing && !state.checking)
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
	if (absolutePath.isEmpty() || !checksAudioService())
		return {};

	return audioServiceProblemText(audioServiceVerdict(absolutePath, configPath));
}

void FileReferenceController::askNothing()
{
	probes.clear();
	presence.clear();
}

namespace
{
// Written at start-up (or by a test) on the UI thread, read there too.
FileReferenceController::Policy installedPolicy;
}

void FileReferenceController::installPolicy(const Policy& policy)
{
	installedPolicy = policy;
}

bool FileReferenceController::checksAudioService()
{
	return installedPolicy.checkAudioService;
}

bool FileReferenceController::probesPresenceInBackground()
{
	return installedPolicy.probePresenceInBackground;
}

FileReferenceController::AudioServiceVerdict FileReferenceController::audioServiceVerdict(
	const QString& absolutePath, const QString& configPath)
{
	const std::wstring path = QDir::toNativeSeparators(absolutePath).toStdWString();
	std::wstring reason;
	if (!ConfigPathPolicy::allowsOpen(path, QDir::toNativeSeparators(configPath).toStdWString(), reason))
		return AudioServiceVerdict::NotOnLocalDrive;
	if (!AudioEngineAccess::isReadableByAudioEngine(path))
		return AudioServiceVerdict::NotReadable;
	return AudioServiceVerdict::Opens;
}

QString FileReferenceController::audioServiceProblemText(AudioServiceVerdict verdict)
{
	switch (verdict)
	{
	case AudioServiceVerdict::NotOnLocalDrive:
		return QCoreApplication::translate("FileReferenceController", "The audio service only opens files on local drives");
	case AudioServiceVerdict::NotReadable:
		return QCoreApplication::translate("FileReferenceController", "Not readable by the audio service");
	case AudioServiceVerdict::Opens:
		break;
	}
	return {};
}
