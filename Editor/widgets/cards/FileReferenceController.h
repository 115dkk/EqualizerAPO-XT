/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

#include "ReferenceCardView.h"

class QWidget;

class FileReferenceController : public QObject
{
public:
	FileReferenceController(const QString& kind, const QString& writtenPath,
		QObject* parent = nullptr);

	const QString& writtenPath() const;
	const QString& resolvedPath() const;
	void setWrittenPath(const QString& path);
	void setResolvedPath(const QString& path);
	// The file the engine opens for the written text (ConfigFileReference):
	// quotes and %VARIABLES% taken, relative to the configuration's folder.
	void resolveAgainstConfig(const QString& configPath);

	// selectVst3Bundles additionally lets the dialog pick *.vst3 bundle
	// directories as if they were files (GUIHelper::enableVst3BundleSelection).
	QString chooseExistingFile(QWidget* parent, const QString& title,
		const QString& initialPath, const QString& nameFilter,
		const QString& referenceBaseDirectory,
		const QString& selectedFile = QString(),
		bool selectVst3Bundles = false);
	ReferenceCardState describe(const QString& emptyName) const;
	// Why the audio service will not open absolutePath, or an empty string
	// when it will: first the engine's own rule for where a line's file may
	// be (ConfigPathPolicy, the check it makes when it loads the line), then
	// LOCAL SERVICE's rights on the file.
	static QString audioServiceProblem(const QString& absolutePath, const QString& configPath);
	bool importIntoConfig(QWidget* parent, const QString& configPath);

	// How the Editor writes a file the user chose, relative to the folder a
	// relative reference is read from: relative unless that means climbing
	// more than one level above it (a path starting with ../../), absolute
	// otherwise. The one rule for every card and legacy row (maintainer
	// decision 2026-09-25, audit #348 open question 2).
	static QString displayPathForBaseDirectory(
		const QString& baseDirectory, const QString& selectedPath);

private:
	QString referenceKind;
	QString written;
	QString resolved;
};
