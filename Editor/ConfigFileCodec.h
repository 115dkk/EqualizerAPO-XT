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

#pragma once

#include <string>
#include <QString>
#include <QList>
#include <QByteArray>

// UI-free reader/writer for EqualizerAPO configuration files: the encoding
// detection (UTF-8 with a system-ANSI fallback when the bytes do not decode
// as UTF-8) and the Win32 sharing-violation retry loop sit behind a real,
// independently testable seam. The class deliberately knows nothing about
// MainWindow or any QWidget: all user-facing reporting (QMessageBox + tr())
// stays in the Editor, which inspects the results returned here.
class ConfigFileCodec
{
public:
	// Outcome of a read attempt. ok == false means the file could not be opened
	// because of a non-sharing-violation error; errorMessage then holds the
	// formatted system error string the caller should surface. A locked file is
	// retried internally and never reported as a failure.
	struct ReadResult
	{
		bool ok = false;
		QList<QString> lines;
		QString errorMessage;
	};

	// Outcome of a write attempt. opened == false means CreateFile failed with a
	// non-sharing-violation error (errorMessage set), so the caller should abort.
	// When opened == true the file was written; comparing bytesWritten against
	// totalBytes lets the caller detect a short write.
	struct WriteResult
	{
		bool opened = false;
		QString errorMessage;
		unsigned long bytesWritten = 0;
		qsizetype totalBytes = 0;
	};

	// Reads path, retrying while the file is locked for writing, then decodes the
	// raw bytes into lines. The path is expected to already use native
	// separators (the caller converts it).
	static ReadResult readConfig(const QString& path);

	// Writes lines to path, retrying while the file is locked, joining lines with
	// CRLF and encoding each line as UTF-8 (no trailing newline).
	static WriteResult writeConfig(const QString& path, const QList<QString>& lines);

	// Pure, file-system-free transforms, exposed for reuse and testing.
	// decodeLines splits the raw file bytes into lines, stripping a trailing CR
	// and decoding UTF-8 with a system-ANSI (CP_ACP) fallback on invalid UTF-8.
	// encodeLines is its inverse: CRLF-joined UTF-8 with no trailing newline.
	static QList<QString> decodeLines(const std::string& bytes);
	static QByteArray encodeLines(const QList<QString>& lines);
};
