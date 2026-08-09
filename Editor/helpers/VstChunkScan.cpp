/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "VstChunkScan.h"

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QString>

#include "services/security/AudioEngineAccess.h"

QStringList vstChunkPathCandidates(const std::wstring& chunkData)
{
	QStringList candidates;
	if (chunkData == L"" || chunkData.length() >= 100000)
		return candidates;

	QByteArray bytes = QByteArray::fromBase64(QString::fromStdWString(chunkData).toUtf8());
	QString chunkText = QString::fromUtf8(bytes.data(), bytes.length());
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
