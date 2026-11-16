/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "Editor/ConfigFileCodec.h"
#include "platform/windows/FileSharingRetry.h"
#include "platform/windows/Win32Resource.h"

namespace
{
class SaveRetryPolicy final : public FileSharingRetryPolicy
{
public:
	SaveRetryPolicy(winutil::UniqueHandle* releaseOnWait, ULONGLONG elapsedPerWait = 0)
		: releaseOnWait_(releaseOnWait), elapsedPerWait_(elapsedPerWait)
	{
	}

	ULONGLONG now() const override
	{
		return elapsedMilliseconds;
	}

	bool wait(HANDLE cancel, DWORD milliseconds) const override
	{
		++waitCount;
		lastRequestedWait = milliseconds;
		cancelHandleWasNull = cancel == nullptr;
		elapsedMilliseconds += elapsedPerWait_ == 0 ? milliseconds : elapsedPerWait_;
		if (releaseOnWait_ != nullptr)
			releaseOnWait_->reset();
		return false;
	}

	mutable ULONGLONG elapsedMilliseconds = 0;
	mutable unsigned waitCount = 0;
	mutable DWORD lastRequestedWait = 0;
	mutable bool cancelHandleWasNull = false;

private:
	winutil::UniqueHandle* releaseOnWait_;
	ULONGLONG elapsedPerWait_;
};
}

TEST_CASE(EditorLogicTests, testConfigFileCodec)
{
	QList<QString> mixed = ConfigFileCodec::decodeLines(std::string("Preamp: -6 dB\r\nInclude: a.txt\nlast"));
	requireEqual((int)mixed.size(), 3, "decodeLines splits CRLF and LF terminated lines");
	expectEqual(mixed[0], "Preamp: -6 dB", "decodeLines strips the trailing CR");
	expectEqual(mixed[2], "last", "decodeLines keeps the final unterminated line");

	QList<QString> unicode = ConfigFileCodec::decodeLines(std::string("# caf\xC3\xA9"));
	expectEqual(unicode[0], QString::fromUtf8("# caf\xC3\xA9"), "decodeLines decodes valid UTF-8");

	// 0xE9 alone is invalid UTF-8, so the system-ANSI fallback must engage.
	// The decoded glyph depends on the machine's CP_ACP (e.g. CP1252 vs
	// CP949), so only the line structure is asserted, not the character.
	QList<QString> fallback = ConfigFileCodec::decodeLines(std::string("caf\xE9\r\nnext"));
	requireEqual((int)fallback.size(), 2, "ANSI fallback still yields one entry per line");
	expectEqual(fallback[1], "next", "ANSI fallback preserves the following line");

	QByteArray encoded = ConfigFileCodec::encodeLines(QList<QString>() << "a" << QString::fromUtf8("caf\xC3\xA9"));
	expectEqual(QString::fromUtf8(encoded), QString::fromUtf8("a\r\ncaf\xC3\xA9"), "encodeLines joins with CRLF, UTF-8, no trailing newline");

	QList<QString> roundTrip = ConfigFileCodec::decodeLines(std::string(encoded.constData(), (size_t)encoded.size()));
	requireEqual((int)roundTrip.size(), 2, "encodeLines output decodes back to the same line count");
	expectEqual(roundTrip[1], QString::fromUtf8("caf\xC3\xA9"), "decode(encode(lines)) round-trips non-ASCII text");

	// A file saved as "UTF-8 with BOM" opens with its first command intact and
	// is saved back without the mark, the form the audio engine reads.
	QList<QString> marked = ConfigFileCodec::decodeLines(std::string("\xEF\xBB\xBFPreamp: -6 dB\r\nnext"));
	requireEqual((int)marked.size(), 2, "decodeLines splits a file that starts with a byte order mark");
	expectEqual(marked[0], "Preamp: -6 dB", "decodeLines drops the byte order mark that starts the file");
	expectEqual(QString::fromUtf8(ConfigFileCodec::encodeLines(marked)), QString("Preamp: -6 dB\r\nnext"),
		"encodeLines writes the file back without a byte order mark");
}

TEST_CASE(EditorLogicTests, testConfigFileCodecRetriesAtomicReplaceAfterReaderCloses)
{
	QTemporaryDir dir;
	requireTrue(dir.isValid(), "atomic-save retry test creates a temporary directory");

	QString path = QDir::toNativeSeparators(dir.filePath("config.txt"));
	QFile original(path);
	requireTrue(original.open(QIODevice::WriteOnly), "atomic-save retry test creates the original file");
	requireTrue(original.write("original") == 8, "atomic-save retry test writes the original contents");
	original.close();

	winutil::UniqueHandle reader(CreateFileW(
		path.toStdWString().c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr));
	requireTrue(static_cast<bool>(reader), "atomic-save retry test holds the engine-style read handle");

	SaveRetryPolicy retryPolicy(&reader);
	ConfigFileCodec::WriteResult result = ConfigFileCodec::writeConfig(
		path, QList<QString>() << "replacement", retryPolicy);

	expectEqual(retryPolicy.waitCount, 1u,
		"writeConfig retries once after the held read handle blocks replacement");
	expectEqual(retryPolicy.lastRequestedWait, DWORD(1),
		"writeConfig starts its retry backoff at one millisecond");
	expectTrue(retryPolicy.cancelHandleWasNull,
		"writeConfig waits without a cancellation handle");
	expectTrue(result.opened, "writeConfig retries until a short configuration read finishes");

	QFile committed(path);
	requireTrue(committed.open(QIODevice::ReadOnly), "atomic-save retry test opens the committed file");
	expectEqual(QString::fromUtf8(committed.readAll()), "replacement",
		"the retry atomically commits the complete replacement");
}

TEST_CASE(EditorLogicTests, testConfigFileCodecPreservesExistingFileWhenAtomicReplaceFails)
{
	QTemporaryDir dir;
	requireTrue(dir.isValid(), "atomic-save test creates a temporary directory");

	QString path = QDir::toNativeSeparators(dir.filePath("config.txt"));
	QFile original(path);
	requireTrue(original.open(QIODevice::WriteOnly), "atomic-save test creates the original file");
	requireTrue(original.write("original") == 8, "atomic-save test writes the original contents");
	original.close();

	// Access 0 with read/write sharing still permits in-place writes, while
	// omitting FILE_SHARE_DELETE prevents replacement while this handle lives.
	winutil::UniqueHandle replacementBlocker(CreateFileW(
		path.toStdWString().c_str(),
		0,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr));
	requireTrue(static_cast<bool>(replacementBlocker), "atomic-save test locks replacement of the original file");

	SaveRetryPolicy retryPolicy(nullptr, 250);
	ConfigFileCodec::WriteResult result = ConfigFileCodec::writeConfig(
		path, QList<QString>() << "replacement", retryPolicy);
	replacementBlocker.reset();

	expectFalse(result.opened, "writeConfig reports an atomic replacement failure");
	expectEqual(retryPolicy.waitCount, 1u,
		"writeConfig stops retrying when the injected clock reaches its limit");
	expectEqual(retryPolicy.elapsedMilliseconds, ULONGLONG(250),
		"writeConfig keeps the 250 millisecond retry window");

	QFile preserved(path);
	requireTrue(preserved.open(QIODevice::ReadOnly), "atomic-save test reopens the original file");
	expectEqual(QString::fromUtf8(preserved.readAll()), "original", "failed atomic replacement preserves the original contents");
}

TEST_CASE(EditorLogicTests, testConfigFileCodecRejectsPartialRead)
{
	const std::wstring pipeName = L"\\\\.\\pipe\\EditorLogicTests-ConfigRead-" + std::to_wstring(GetCurrentProcessId());
	winutil::UniqueHandle pipe(CreateNamedPipeW(
		pipeName.c_str(),
		PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		0,
		nullptr));
	requireTrue(static_cast<bool>(pipe), "partial-read test creates a named pipe");

	bool serverSucceeded = false;
	std::thread server([&]() {
		BOOL connected = ConnectNamedPipe(pipe.get(), nullptr);
		if (!connected && GetLastError() == ERROR_PIPE_CONNECTED)
			connected = TRUE;
		const char prefix[] = "Preamp: -6 dB\r\n";
		DWORD written = 0;
		if (connected && WriteFile(pipe.get(), prefix, sizeof(prefix) - 1, &written, nullptr) && written == sizeof(prefix) - 1)
			serverSucceeded = FlushFileBuffers(pipe.get()) != FALSE;
		DisconnectNamedPipe(pipe.get());
	});

	ConfigFileCodec::ReadResult result = ConfigFileCodec::readConfig(QString::fromStdWString(pipeName));
	server.join();
	requireTrue(serverSucceeded, "partial-read test sends the configuration prefix");
	expectFalse(result.ok, "readConfig rejects bytes followed by a ReadFile failure");
	expectTrue(result.lines.isEmpty(), "readConfig does not expose a partial configuration");
}
