/*
	This file is part of EqualizerAPO-XT.
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
	void resolveAgainstConfig(const QString& configPath);
	void resolveAgainstDirectory(const QString& directoryPath);

	// selectVst3Bundles additionally lets the dialog pick *.vst3 bundle
	// directories as if they were files (GUIHelper::enableVst3BundleSelection).
	QString chooseExistingFile(QWidget* parent, const QString& title,
		const QString& initialPath, const QString& nameFilter,
		const QString& referenceBaseDirectory,
		const QString& selectedFile = QString(),
		bool selectVst3Bundles = false);
	ReferenceCardState describe(const QString& emptyName) const;
	static bool isReadableByAudioService(const QString& absolutePath);
	bool importIntoConfig(QWidget* parent, const QString& configPath);

	static QString displayPathForBaseDirectory(
		const QString& baseDirectory, const QString& selectedPath);

private:
	QString referenceKind;
	QString written;
	QString resolved;
};
