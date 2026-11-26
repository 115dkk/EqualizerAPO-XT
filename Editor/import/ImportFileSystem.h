/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDir>
#include <QString>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace EqAPO::Import
{

// QFileInfo::isSymLink does not identify every directory junction. Check
// the entry itself before enumerating a directory or copying a file.
inline bool isReparsePoint(const QString& path, DWORD requiredAttributes = 0)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    const DWORD mask = FILE_ATTRIBUTE_REPARSE_POINT | requiredAttributes;
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & mask) == mask;
}

}
