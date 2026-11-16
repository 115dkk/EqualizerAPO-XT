/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VstChunkScan.h"

#include "vst/VSTChunkBase64.h"
#include <QFile>
#include <QRegularExpression>
#include <QString>

#include "services/security/AudioEngineAccess.h"

QStringList vstChunkPathCandidates(const std::wstring& chunkData)
{
	QStringList candidates;
	if (chunkData == L"" || chunkData.length() >= 100000)
		return candidates;

	std::vector<char> bytes;
	if (!vstchunk::decodeBase64(chunkData, bytes))
		return candidates;
	QString chunkText = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
	QRegularExpression regexp("[A-Za-z]:(?:\\\\[\\w \\(\\)-]+)+\\.[A-Za-z]{3}");
	QRegularExpressionMatchIterator it = regexp.globalMatch(chunkText);
	while (it.hasNext())
		candidates.append(it.next().captured());
	return candidates;
}

QStringList vstChunkUnreadablePaths(const std::wstring& chunkData)
{
	QStringList files;
	for (const QString& path : vstChunkPathCandidates(chunkData))
	{
		QFile file(path);
		if (file.exists())
		{
			if (!AudioEngineAccess::isReadableByAudioEngine(path.toStdWString()))
				files.append(path);
		}
	}
	files.removeDuplicates();
	return files;
}
