/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FileReferenceController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "services/registry/RegistryPaths.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportDialog.h"
#include "Editor/import/ImportExecutor.h"

namespace
{
// The existing-file dialog every reference chooser opens. skinned dresses it
// in the active skin; the frozen legacy rows keep the stock dialog.
QString runExistingFileDialog(QWidget* parent, const QString& title, const QString& initialPath,
	const QString& nameFilter, const QString& selectedFile, bool selectVst3Bundles, bool skinned)
{
	QFileDialog dialog(parent, title, initialPath, nameFilter);
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(nameFilter);
	if (skinned)
		GUIHelper::prepareFileDialog(dialog);
	if (selectVst3Bundles)
		GUIHelper::enableVst3BundleSelection(dialog);
	if (!selectedFile.isEmpty())
		dialog.selectFile(selectedFile);
	if (dialog.exec() != QDialog::Accepted)
		return {};
	return dialog.selectedFiles().first();
}
}

QString FileReferenceController::chooseExistingFile(QWidget* parent,
	const QString& title, const QString& initialPath,
	const QString& nameFilter, const QString& referenceBaseDirectory,
	const QString& selectedFile, bool selectVst3Bundles)
{
	const QString selected = runExistingFileDialog(parent, title, initialPath, nameFilter,
		selectedFile, selectVst3Bundles, /*skinned=*/ true);
	if (selected.isEmpty())
		return {};

	adoptChosenFile(selected, referenceBaseDirectory);
	return selected;
}

QString FileReferenceController::chooseVstLibrary(QWidget* parent, const QString& title,
	const QString& nameFilter, const QString& writtenPath, const QString& pluginsDirectory, bool skinned)
{
	const QDir pluginsDir(pluginsDirectory);

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	if (!writtenPath.isEmpty())
		fileInfo.setFile(pluginsDir, writtenPath);

	const QString selected = runExistingFileDialog(parent, title, fileInfo.absoluteFilePath(), nameFilter,
		writtenPath.isEmpty() ? QString() : fileInfo.fileName(), /*selectVst3Bundles=*/ true, skinned);
	if (!selected.isEmpty())
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(selected).absolutePath()));
	return selected;
}

bool FileReferenceController::importIntoConfig(
	QWidget* parent, const QString& configPath)
{
	if (resolved.isEmpty() || !QFileInfo::exists(resolved))
		return false;
	const QString configDir = QFileInfo(configPath).absolutePath();
	EqAPO::Import::ImportManifest manifest =
		EqAPO::Import::ConfigDependencyScanner::scan(resolved, configDir);
	if (manifest.items.isEmpty())
	{
		QMessageBox::warning(parent,
			QCoreApplication::translate("FileReferenceController", "Import"),
			QCoreApplication::translate("FileReferenceController", "Nothing to import: %1")
				.arg(manifest.warnings.join('\n')));
		return false;
	}
	EqAPO::Import::ImportDialog dialog(manifest, configDir, parent);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	const EqAPO::Import::ExecutionResult result =
		EqAPO::Import::ImportExecutor::execute(manifest, configDir);
	if (!result.success)
	{
		QMessageBox::warning(parent,
			QCoreApplication::translate("FileReferenceController", "Import"),
			QCoreApplication::translate("FileReferenceController", "Some files could not be copied:\n%1")
				.arg(result.errors.join('\n')));
		return false;
	}
	written = QDir::toNativeSeparators(manifest.rootDest);
	resolveAgainstConfig(configPath);
	return true;
}
