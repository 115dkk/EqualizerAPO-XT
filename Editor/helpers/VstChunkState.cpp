/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VstChunkState.h"

#include <QByteArray>
#include <QString>

namespace
{
quint16 readU16(const QByteArray& bytes, qsizetype offset)
{
	const auto low = static_cast<quint8>(bytes[offset]);
	const auto high = static_cast<quint8>(bytes[offset + 1]);
	return static_cast<quint16>(low | (static_cast<quint16>(high) << 8));
}

bool hasSignature(const QByteArray& bytes, qsizetype offset, quint8 second, quint8 third)
{
	return static_cast<quint8>(bytes[offset]) == 0x50 && static_cast<quint8>(bytes[offset + 1]) == 0x4B
		&& static_cast<quint8>(bytes[offset + 2]) == second && static_cast<quint8>(bytes[offset + 3]) == third;
}

void zeroRange(QByteArray& bytes, qsizetype start, qsizetype length)
{
	for (qsizetype offset = start; offset < start + length; offset++)
		bytes[offset] = '\0';
}

void maskExtraFields(QByteArray& bytes, qsizetype start, qsizetype length)
{
	const qsizetype end = start + length;
	qsizetype field = start;
	while (end - field >= 4)
	{
		const quint16 id = readU16(bytes, field);
		const quint16 size = readU16(bytes, field + 2);
		const qsizetype data = field + 4;
		if (static_cast<qsizetype>(size) > end - data)
			break;

		if (id == 0x5455 && size > 1)
			zeroRange(bytes, data + 1, static_cast<qsizetype>(size) - 1);
		else if (id == 0x000A && size > 4)
			zeroRange(bytes, data + 4, static_cast<qsizetype>(size) - 4);
		else if (id == 0x5855 && size > 0)
			zeroRange(bytes, data, size < 8 ? static_cast<qsizetype>(size) : 8);

		field = data + size;
	}
}

void maskZipTimestamps(QByteArray& bytes)
{
	const qsizetype length = bytes.size();
	if (length < 4)
		return;

	for (qsizetype offset = 0; offset <= length - 4; offset++)
	{
		if (hasSignature(bytes, offset, 0x03, 0x04))
		{
			if (length - offset < 30)
				continue;

			const quint16 nameLength = readU16(bytes, offset + 26);
			const quint16 extraLength = readU16(bytes, offset + 28);
			const qsizetype variableLength = static_cast<qsizetype>(nameLength) + extraLength;
			if (variableLength > length - offset - 30)
				continue;

			zeroRange(bytes, offset + 10, 4);
			maskExtraFields(bytes, offset + 30 + nameLength, extraLength);
		}
		else if (hasSignature(bytes, offset, 0x01, 0x02))
		{
			if (length - offset < 46)
				continue;

			const quint16 nameLength = readU16(bytes, offset + 28);
			const quint16 extraLength = readU16(bytes, offset + 30);
			const quint16 commentLength = readU16(bytes, offset + 32);
			const qsizetype variableLength = static_cast<qsizetype>(nameLength) + extraLength + commentLength;
			if (variableLength > length - offset - 46)
				continue;

			zeroRange(bytes, offset + 12, 4);
			maskExtraFields(bytes, offset + 46 + nameLength, extraLength);
		}
	}
}
} // namespace

bool vstChunksEquivalent(const std::wstring& first, const std::wstring& second)
{
	if (first == second)
		return true;

	auto firstResult = QByteArray::fromBase64Encoding(
		QString::fromStdWString(first).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	auto secondResult = QByteArray::fromBase64Encoding(
		QString::fromStdWString(second).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	if (!firstResult || !secondResult)
		return false;
	if (firstResult.decoded.size() != secondResult.decoded.size())
		return false;

	maskZipTimestamps(firstResult.decoded);
	maskZipTimestamps(secondResult.decoded);
	return firstResult.decoded == secondResult.decoded;
}
