/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "Editor/helpers/VstChunkState.h"

#include <string>

#include <QByteArray>
#include <QString>

#include "EditorLogicTestSupport.h"

namespace
{
void appendU16(QByteArray& bytes, quint16 value)
{
	bytes.append(static_cast<char>(value & 0xFF));
	bytes.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32(QByteArray& bytes, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8)
		bytes.append(static_cast<char>((value >> shift) & 0xFF));
}

void appendU64(QByteArray& bytes, quint64 value)
{
	for (int shift = 0; shift < 64; shift += 8)
		bytes.append(static_cast<char>((value >> shift) & 0xFF));
}

quint32 crc32(const QByteArray& content)
{
	quint32 crc = 0xFFFFFFFFU;
	for (const char byte : content)
	{
		crc ^= static_cast<quint8>(byte);
		for (int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
	}
	return ~crc;
}

// streamed: general purpose bit 3, so the local header carries zero CRC and
// sizes and a data descriptor after the data holds them.
QByteArray makeArchive(const QByteArray& name, const QByteArray& content, quint16 dosTime, quint16 dosDate,
	const QByteArray& extra = {}, const QByteArray& prefix = {}, bool streamed = false)
{
	QByteArray bytes = prefix;
	const qsizetype archiveStart = bytes.size();
	const quint32 checksum = crc32(content);
	const quint32 contentSize = static_cast<quint32>(content.size());
	const quint16 nameLength = static_cast<quint16>(name.size());
	const quint16 extraLength = static_cast<quint16>(extra.size());
	const quint16 flags = streamed ? 0x0808 : 0;

	appendU32(bytes, 0x04034B50U);
	appendU16(bytes, 20);
	appendU16(bytes, flags);
	appendU16(bytes, 0);
	appendU16(bytes, dosTime);
	appendU16(bytes, dosDate);
	appendU32(bytes, streamed ? 0 : checksum);
	appendU32(bytes, streamed ? 0 : contentSize);
	appendU32(bytes, streamed ? 0 : contentSize);
	appendU16(bytes, nameLength);
	appendU16(bytes, extraLength);
	bytes.append(name);
	bytes.append(extra);
	bytes.append(content);
	if (streamed)
	{
		appendU32(bytes, 0x08074B50U);
		appendU32(bytes, checksum);
		appendU32(bytes, contentSize);
		appendU32(bytes, contentSize);
	}

	const quint32 centralOffset = static_cast<quint32>(bytes.size() - archiveStart);
	const qsizetype centralStart = bytes.size();
	appendU32(bytes, 0x02014B50U);
	appendU16(bytes, 20);
	appendU16(bytes, 20);
	appendU16(bytes, flags);
	appendU16(bytes, 0);
	appendU16(bytes, dosTime);
	appendU16(bytes, dosDate);
	appendU32(bytes, checksum);
	appendU32(bytes, contentSize);
	appendU32(bytes, contentSize);
	appendU16(bytes, nameLength);
	appendU16(bytes, extraLength);
	appendU16(bytes, 0);
	appendU16(bytes, 0);
	appendU16(bytes, 0);
	appendU32(bytes, 0);
	appendU32(bytes, 0);
	bytes.append(name);
	bytes.append(extra);

	const quint32 centralSize = static_cast<quint32>(bytes.size() - centralStart);
	appendU32(bytes, 0x06054B50U);
	appendU16(bytes, 0);
	appendU16(bytes, 0);
	appendU16(bytes, 1);
	appendU16(bytes, 1);
	appendU32(bytes, centralSize);
	appendU32(bytes, centralOffset);
	appendU16(bytes, 0);
	return bytes;
}

QByteArray extendedTimestampExtra(quint32 modifiedTime)
{
	QByteArray extra;
	appendU16(extra, 0x5455);
	appendU16(extra, 5);
	extra.append(static_cast<char>(1));
	appendU32(extra, modifiedTime);
	return extra;
}

QByteArray ntfsTimestampExtra(quint64 modifiedTime)
{
	QByteArray extra;
	appendU16(extra, 0x000A);
	appendU16(extra, 32);
	appendU32(extra, 0);
	appendU16(extra, 1);
	appendU16(extra, 24);
	appendU64(extra, modifiedTime);
	appendU64(extra, modifiedTime + 1);
	appendU64(extra, modifiedTime + 2);
	return extra;
}

std::wstring encodeChunk(const QByteArray& bytes)
{
	return QString::fromLatin1(bytes.toBase64()).toStdWString();
}
} // namespace

TEST_CASE(EditorLogicTests, testVstChunkStateIdenticalAndEmptyInputs)
{
	harness.expectTrue(vstChunksEquivalent(L"!!!!", L"!!!!"),
		"identical strings are equivalent even when they are not valid base64");
	harness.expectTrue(vstChunksEquivalent(L"", L""), "two empty strings are equivalent");
	harness.expectFalse(vstChunksEquivalent(L"", encodeChunk(QByteArray("x"))),
		"an empty chunk and a non-empty chunk are not equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateDosTimestamps)
{
	const std::wstring first = encodeChunk(makeArchive("state.json", "{\"mix\":0.5}", 0x1020, 0x5021));
	const std::wstring second = encodeChunk(makeArchive("state.json", "{\"mix\":0.5}", 0x2841, 0x5862));
	harness.expectTrue(vstChunksEquivalent(first, second),
		"archives that differ only in local and central DOS timestamps are equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateDataDescriptorArchive)
{
	// The layout read from Kilohearts kHs Delay 2.4.6: one state.json entry
	// written with a data descriptor, and two reads a second apart that differ
	// in the low byte of the DOS time only (it counts two-second steps).
	const std::wstring first = encodeChunk(makeArchive("state.json", "{\"mix\":0.5}", 0x75D5, 0x5D6A, {}, {}, true));
	const std::wstring second = encodeChunk(makeArchive("state.json", "{\"mix\":0.5}", 0x75D6, 0x5D6A, {}, {}, true));
	harness.expectTrue(vstChunksEquivalent(first, second),
		"archives written with a data descriptor that differ only in the DOS time are equivalent");

	const std::wstring changed = encodeChunk(makeArchive("state.json", "{\"mix\":0.7}", 0x75D6, 0x5D6A, {}, {}, true));
	harness.expectFalse(vstChunksEquivalent(first, changed),
		"a changed value of the same length is not equivalent: its CRC in the data descriptor and the central header differs");
}

TEST_CASE(EditorLogicTests, testVstChunkStateExtendedTimestamps)
{
	const std::wstring first = encodeChunk(
		makeArchive("state.json", "same", 0x1020, 0x5021, extendedTimestampExtra(1700000000U)));
	const std::wstring second = encodeChunk(
		makeArchive("state.json", "same", 0x1020, 0x5021, extendedTimestampExtra(1800000000U)));
	harness.expectTrue(vstChunksEquivalent(first, second),
		"archives that differ only in 0x5455 extended timestamps are equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateNtfsTimestamps)
{
	const std::wstring first = encodeChunk(
		makeArchive("state.json", "same", 0x1020, 0x5021, ntfsTimestampExtra(132000000000000000ULL)));
	const std::wstring second = encodeChunk(
		makeArchive("state.json", "same", 0x1020, 0x5021, ntfsTimestampExtra(133000000000000000ULL)));
	harness.expectTrue(vstChunksEquivalent(first, second),
		"archives that differ only in 0x000A NTFS timestamps are equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateContentAndNameChanges)
{
	const std::wstring contentA = encodeChunk(makeArchive("state.json", "value-a", 0x1020, 0x5021));
	const std::wstring contentB = encodeChunk(makeArchive("state.json", "value-b", 0x1020, 0x5021));
	harness.expectFalse(vstChunksEquivalent(contentA, contentB),
		"a changed content byte and its changed CRC are not equivalent");

	const std::wstring nameA = encodeChunk(makeArchive("state.json", "same", 0x1020, 0x5021));
	const std::wstring nameB = encodeChunk(makeArchive("state.jsox", "same", 0x1020, 0x5021));
	harness.expectFalse(vstChunksEquivalent(nameA, nameB),
		"entry names of the same length that differ by one letter are not equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStatePrefixedArchive)
{
	const QByteArray prefix(16, static_cast<char>(0x5A));
	const std::wstring first = encodeChunk(makeArchive("state.json", "same", 0x1020, 0x5021, {}, prefix));
	const std::wstring second = encodeChunk(makeArchive("state.json", "same", 0x2841, 0x5862, {}, prefix));
	harness.expectTrue(vstChunksEquivalent(first, second),
		"an archive after a 16-byte VST3-style prefix is equivalent when only ZIP times differ");

	QByteArray changedPrefix = prefix;
	changedPrefix[7] = static_cast<char>(0x5B);
	const std::wstring changed = encodeChunk(makeArchive("state.json", "same", 0x2841, 0x5862, {}, changedPrefix));
	harness.expectFalse(vstChunksEquivalent(first, changed),
		"a changed byte in the prefix before an embedded archive is not equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateNonZipAndLengthChanges)
{
	harness.expectFalse(vstChunksEquivalent(encodeChunk("plain-a"), encodeChunk("plain-b")),
		"equal-length non-ZIP bytes that differ are not equivalent");
	harness.expectFalse(vstChunksEquivalent(encodeChunk("short"), encodeChunk("longer")),
		"decoded buffers of different lengths are not equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateInvalidBase64)
{
	harness.expectFalse(vstChunksEquivalent(L"!!!!", encodeChunk("valid")),
		"an invalid base64 string and a valid base64 string are not equivalent");
}

TEST_CASE(EditorLogicTests, testVstChunkStateTruncatedHeader)
{
	QByteArray first("prefix");
	appendU32(first, 0x04034B50U);
	first.append("abcde", 5);
	QByteArray second = first;
	second[second.size() - 1] = 'f';
	harness.expectFalse(vstChunksEquivalent(encodeChunk(first), encodeChunk(second)),
		"a differing byte in a truncated local header remains significant without out-of-range access");
}
