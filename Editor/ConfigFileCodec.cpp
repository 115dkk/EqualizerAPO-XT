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

#include <sstream>

#include <QSaveFile>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "helpers/FileSharingRetry.h"
#include "helpers/StringHelper.h"
#include "ConfigFileCodec.h"

using std::string;
using std::stringstream;
using std::wstring;

QList<QString> ConfigFileCodec::decodeLines(const string& bytes)
{
	stringstream inputStream;
	inputStream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	inputStream.seekg(0);

	QList<QString> lines;
	while (inputStream.good())
	{
		string encodedLine;
		getline(inputStream, encodedLine);
		if (encodedLine.size() > 0 && encodedLine[encodedLine.size() - 1] == '\r')
			encodedLine.resize(encodedLine.size() - 1);

		wstring line = StringHelper::toWString(encodedLine, CP_UTF8);
		if (line.find(L'\uFFFD') != wstring::npos)
			line = StringHelper::toWString(encodedLine, CP_ACP);

		lines.append(QString::fromStdWString(line));
	}

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
	HANDLE hFile = openFileWithSharingRetry(path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error);
	if (hFile == INVALID_HANDLE_VALUE)
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
		if (!ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr))
		{
			error = GetLastError();
			CloseHandle(hFile);
			result.ok = false;
			result.errorMessage = QString::fromStdWString(StringHelper::getSystemErrorString(error));
			return result;
		}
		if (bytesRead == 0)
			break;
		buffer.append(buf, bytesRead);
	}

	CloseHandle(hFile);

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
