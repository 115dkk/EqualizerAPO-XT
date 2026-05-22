#include "ConvolutionPathHelper.h"

#include <QDir>
#include <QFileInfo>

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
	if (path.isEmpty())
		return QString();

	QString normalizedPath = QDir::fromNativeSeparators(path);
	if (QDir::isAbsolutePath(normalizedPath))
		return QDir::cleanPath(normalizedPath);

	return QDir::cleanPath(configDirectory(configPath).absoluteFilePath(normalizedPath));
}

QString ConvolutionPathHelper::displayPathForSelection(const QString& configPath, const QString& selectedPath)
{
	QString absolutePath = absolutePathForConfig(configPath, selectedPath);
	if (absolutePath.isEmpty())
		return QString();

	QDir configDir = configDirectory(configPath);
	QString relativePath = configDir.relativeFilePath(absolutePath);
	if (relativePathStaysInConfigDirectory(relativePath))
		return QDir::toNativeSeparators(relativePath);

	return QDir::toNativeSeparators(absolutePath);
}

bool ConvolutionPathHelper::relativePathStaysInConfigDirectory(const QString& relativePath)
{
	QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
	return !cleanPath.isEmpty() && cleanPath != ".." && !cleanPath.startsWith("../") && !QDir::isAbsolutePath(cleanPath);
}
