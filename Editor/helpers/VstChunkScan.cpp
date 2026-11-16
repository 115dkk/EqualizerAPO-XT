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
	return vstChunkUnreadablePaths(chunkData,
		[](const QString& path) { return QFile(path).exists(); },
		[](const QString& path) { return AudioEngineAccess::isReadableByAudioEngine(path.toStdWString()); });
}

QStringList vstChunkUnreadablePaths(const std::wstring& chunkData,
	const std::function<bool(const QString&)>& exists,
	const std::function<bool(const QString&)>& readableByAudioService)
{
	QStringList files;
	for (const QString& path : vstChunkPathCandidates(chunkData))
	{
		if (exists(path))
		{
			if (!readableByAudioService(path))
				files.append(path);
		}
	}
	files.removeDuplicates();
	return files;
}
