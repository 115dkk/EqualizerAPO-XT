/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonDocument>
#include <QString>

class UpdateInfoFormatter
{
public:
	// The newest (first-listed) version in the update document, or an empty
	// string. Split from releaseHtml (audit #275 B8): the skipped-version
	// check used to render the whole release HTML just to throw it away for
	// this one value.
	static QString newestVersion(const QJsonDocument& doc);

	static QString releaseHtml(const QJsonDocument& doc, QString* newestVersion = nullptr);
};
