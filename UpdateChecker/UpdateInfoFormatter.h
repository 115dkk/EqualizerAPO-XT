#pragma once

#include <QJsonDocument>
#include <QString>

class UpdateInfoFormatter
{
public:
	static QString releaseHtml(const QJsonDocument& doc, QString* newestVersion);
};
