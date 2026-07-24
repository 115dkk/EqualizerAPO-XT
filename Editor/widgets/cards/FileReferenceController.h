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

	QString chooseExistingFile(QWidget* parent, const QString& title,
		const QString& initialPath, const QString& nameFilter,
		const QString& referenceBaseDirectory,
		const QString& selectedFile = QString());
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
