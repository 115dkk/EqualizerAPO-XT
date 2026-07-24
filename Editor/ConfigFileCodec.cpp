/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QSaveFile>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "helpers/FileSharingRetry.h"
#include "helpers/StringHelper.h"
#include "ConfigurationFileReader.h"
#include "ConfigFileCodec.h"

using std::string;

QList<QString> ConfigFileCodec::decodeLines(const string& bytes)
{
	QList<QString> lines;
	for (const std::wstring& line : ConfigurationFileReader::decodeLines(bytes))
		lines.append(QString::fromStdWString(line));
	return lines;
}

QByteArray ConfigFileCodec::encodeLines(const QList<QString>& lines)
{
	bool first = true;
	QByteArray byteArray;
	for (const QString& line : lines)
	{
		if (first)
			first = false;
		else
			byteArray.append("\r\n");
		byteArray.append(line.toUtf8());
	}

	return byteArray;
}

ConfigFileCodec::ReadResult ConfigFileCodec::readConfig(const QString& path)
{
	ReadResult result;

	DWORD error = ERROR_SUCCESS;
	winutil::UniqueHandle file = openFileWithSharingRetry(
		path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error);
	if (!file)
	{
		result.ok = false;
		result.errorMessage = QString::fromStdWString(StringHelper::getSystemErrorString(error));
		return result;
	}

	string buffer;

	char buf[8192];
	for (;;)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(file.get(), buf, sizeof(buf), &bytesRead, nullptr))
		{
			error = GetLastError();
			result.ok = false;
			result.errorMessage = QString::fromStdWString(StringHelper::getSystemErrorString(error));
			return result;
		}
		if (bytesRead == 0)
			break;
		buffer.append(buf, bytesRead);
	}

	result.ok = true;
	result.lines = decodeLines(buffer);
	return result;
}

ConfigFileCodec::WriteResult ConfigFileCodec::writeConfig(const QString& path, const QList<QString>& lines)
{
	WriteResult result;

	QByteArray byteArray = encodeLines(lines);
	result.totalBytes = byteArray.length();

	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly))
	{
		result.opened = false;
		result.errorMessage = file.errorString();
		return result;
	}

	const qint64 bytesWritten = file.write(byteArray);
	if (bytesWritten != byteArray.size())
	{
		file.cancelWriting();
		result.opened = false;
		result.bytesWritten = bytesWritten > 0 ? static_cast<unsigned long>(bytesWritten) : 0;
		result.errorMessage = file.errorString();
		if (result.errorMessage.isEmpty())
			result.errorMessage = QStringLiteral("Only %1/%2 bytes could be written").arg(bytesWritten).arg(byteArray.size());
		return result;
	}

	result.bytesWritten = static_cast<unsigned long>(bytesWritten);
	if (!file.commit())
	{
		result.opened = false;
		result.errorMessage = file.errorString();
		return result;
	}

	result.opened = true;
	return result;
}
