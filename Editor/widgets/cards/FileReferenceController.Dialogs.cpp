#include "FileReferenceController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportDialog.h"
#include "Editor/import/ImportExecutor.h"
#include "services/security/AudioEngineAccess.h"

QString FileReferenceController::chooseExistingFile(QWidget* parent,
	const QString& title, const QString& initialPath,
	const QString& nameFilter, const QString& referenceBaseDirectory,
	const QString& selectedFile)
{
	QFileDialog dialog(parent, title, initialPath, nameFilter);
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(nameFilter);
	GUIHelper::prepareFileDialog(dialog);
	if (!selectedFile.isEmpty())
		dialog.selectFile(selectedFile);
	if (dialog.exec() != QDialog::Accepted)
		return {};

	const QString selected = dialog.selectedFiles().first();
	written = referenceBaseDirectory.isEmpty()
		? QDir::toNativeSeparators(selected)
		: displayPathForBaseDirectory(referenceBaseDirectory, selected);
	resolved = QDir::toNativeSeparators(selected);
	return selected;
}

bool FileReferenceController::isReadableByAudioService(const QString& absolutePath)
{
	if (absolutePath.isEmpty() || qEnvironmentVariableIsSet("EAPO_SKIN_GALLERY"))
		return true;
	return AudioEngineAccess::isReadableByAudioEngine(
		QDir::toNativeSeparators(absolutePath).toStdWString());
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
