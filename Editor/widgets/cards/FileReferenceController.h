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
	// Takes a file the user picked as this reference: written relative to
	// referenceBaseDirectory by displayPathForBaseDirectory (as picked when
	// that is empty), resolved to the picked file.
	void adoptChosenFile(const QString& selectedPath, const QString& referenceBaseDirectory);
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
	// How both VST rows show and write a loaded library's path: the same rule
	// against the default VSTPlugins directory (pluginsDirectory), except that
	// the absolute fallback keeps the library path exactly as given.
	static QString displayPathForLibrary(const QString& libraryPath, const QString& pluginsDirectory);
	// The legacy convolution rows' resolution of a written path: the file the
	// engine opens for path in configPath's line (ConfigFileReference), with
	// '/' separators; empty when path names nothing.
	static QString absolutePathForConfig(const QString& configPath, const QString& path);
	// How a legacy convolution row writes a chosen file: resolved like
	// absolutePathForConfig, then written by displayPathForBaseDirectory
	// against the configuration's folder.
	static QString displayPathForConfigSelection(const QString& configPath, const QString& selectedPath);

	// The plug-in library chooser both VST rows open. It starts at the written
	// path (read against pluginsDirectory) or else in the folder the last
	// library was picked from (vst/lastDir, falling back to pluginsDirectory),
	// lets *.vst3 bundles be picked like files, and remembers the picked
	// file's folder. skinned dresses the dialog in the active skin (the card);
	// the frozen legacy row keeps the stock dialog. Returns the picked file,
	// or an empty string when the dialog was cancelled.
	static QString chooseVstLibrary(QWidget* parent, const QString& title, const QString& nameFilter,
		const QString& writtenPath, const QString& pluginsDirectory, bool skinned);

private:
	QString referenceKind;
	QString written;
	QString resolved;
};
