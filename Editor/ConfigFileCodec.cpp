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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "helpers/StringHelper.h"
#include "ConfigFileCodec.h"

using std::string;
using std::stringstream;
using std::wstring;

QList<QString> ConfigFileCodec::decodeLines(const string& bytes)
{
	// Re-create the same stream the inline loader used: write the raw bytes,
	// then rewind so getline reads from the start. Reading from a stringstream
	// built this way is byte-for-byte equivalent to the previous code path.
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
	for (QString line : lines)
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

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				result.ok = false;
				result.errorMessage = QString::fromStdWString(StringHelper::getSystemErrorString(error));
				return result;
			}

			// file is being written, so wait
			Sleep(1);
		}
	}

	string buffer;

	char buf[8192];
	unsigned long bytesRead = -1;
	while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead != 0)
	{
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

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				result.opened = false;
				result.errorMessage = QString::fromStdWString(StringHelper::getSystemErrorString(error));
				return result;
			}

			// file is being written, so wait
			Sleep(1);
		}
	}

	unsigned long bytesWritten;
	WriteFile(hFile, byteArray.constData(), byteArray.length(), &bytesWritten, nullptr);

	CloseHandle(hFile);

	result.opened = true;
	result.bytesWritten = bytesWritten;
	return result;
}
