#pragma once

#include <QString>

class ConvolutionPathHelper
{
public:
	static QString absolutePathForConfig(const QString& configPath, const QString& path);
	static QString displayPathForSelection(const QString& configPath, const QString& selectedPath);
	static bool relativePathStaysInConfigDirectory(const QString& relativePath);
};
