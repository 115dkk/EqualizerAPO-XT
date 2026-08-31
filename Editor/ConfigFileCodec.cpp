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
#include "platform/windows/Win32Error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/FileSharingRetry.h"
#include "services/logging/Logging.h"
#include "engine/ConfigurationFileReader.h"
#include "ConfigFileCodec.h"

using std::string;

namespace
{
constexpr ULONGLONG atomicCommitRetryWindowMs = 250;

void logWriteFailure(const QString& path, const wchar_t* stage, const QString& message)
{
	const std::wstring nativePath = path.toStdWString();
	const std::wstring nativeMessage = message.toStdWString();
	LogFStatic(
		L"[Editor] configuration save failed during %s for %s: %s",
		stage,
		nativePath.c_str(),
		nativeMessage.c_str());
}
}

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
		path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error, nullptr);
	if (!file)
	{
		result.ok = false;
		result.errorMessage = QString::fromStdWString(win32::errorMessage(error));
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
			result.errorMessage = QString::fromStdWString(win32::errorMessage(error));
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

	int commitFailures = 0;
	ULONGLONG retryDeadline = 0;
	for (;;)
	{
		QSaveFile file(path);
		file.setDirectWriteFallback(false);
		if (!file.open(QIODevice::WriteOnly))
		{
			result.opened = false;
			result.errorMessage = file.errorString();
			logWriteFailure(path, L"temporary-file open", result.errorMessage);
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
			logWriteFailure(path, L"temporary-file write", result.errorMessage);
			return result;
		}

		result.bytesWritten = static_cast<unsigned long>(bytesWritten);
		if (file.commit())
		{
			if (commitFailures != 0)
				TraceFStatic(L"[Editor] configuration save succeeded after %d atomic-commit retries", commitFailures);
			result.opened = true;
			return result;
		}

		result.opened = false;
		result.errorMessage = file.errorString();
		if (file.error() != QFileDevice::RenameError)
		{
			logWriteFailure(path, L"atomic commit", result.errorMessage);
			return result;
		}

		++commitFailures;
		const ULONGLONG now = GetTickCount64();
		if (retryDeadline == 0)
			retryDeadline = now + atomicCommitRetryWindowMs;
		else if (now >= retryDeadline)
		{
			logWriteFailure(path, L"atomic commit", result.errorMessage);
			return result;
		}
		Sleep(1);
	}
}
