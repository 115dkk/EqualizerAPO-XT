/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "UpdateInfoFormatter.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>

QString UpdateInfoFormatter::newestVersion(const QJsonDocument& doc)
{
	const QJsonArray versionsArray = doc.object().value("versions").toArray();
	if (versionsArray.isEmpty())
		return QString();
	return versionsArray.first().toObject().value("version").toString();
}

QString UpdateInfoFormatter::releaseHtml(const QJsonDocument& doc, QString* newestVersion)
{
	if (newestVersion != nullptr)
		newestVersion->clear();

	QJsonObject docObj = doc.object();
	QJsonArray versionsArray = docObj.value("versions").toArray();
	QString html = "<style>\n.date{font-size:small;font-style:italic;color:gray;}\nul{margin:5px;}\nli{margin:2px;}\n</style>\n";

	bool first = true;
	for (QJsonValue versionValue : versionsArray)
	{
		QJsonObject versionObj = versionValue.toObject();
		QString version = versionObj.value("version").toString();
		if (first)
		{
			if (newestVersion != nullptr)
				*newestVersion = version;
			first = false;
		}

		QString rawDate = versionObj.value("date").toString();
		QDate date = QDate::fromString(rawDate, Qt::ISODate);
		QString dateText = date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : rawDate.toHtmlEscaped();

		html.append(QString("<div><b>%0 </b><span class=\"date\">(%1)</span></div>")
			.arg(version.toHtmlEscaped(), dateText));
		html.append("<ul>");

		QJsonArray infoArray = versionObj.value("info").toArray();
		for (QJsonValue v : infoArray)
			html.append("<li>" + v.toString().toHtmlEscaped() + "</li>");
		html.append("</ul>");
	}

	return html;
}
