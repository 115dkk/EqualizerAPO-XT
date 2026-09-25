/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The pipe descriptors of platform/windows/NamedPipeSecurity.h (audit #348
	TD-46), checked against Windows itself: what the ACEs say, and what an
	account holding only the device test pipe's LOCAL SERVICE grant can and
	cannot do with the pipe. The last part runs with the current user standing
	in for LOCAL SERVICE, because the grant is the same mask either way and a
	test cannot become LOCAL SERVICE.
*/

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

#include "platform/windows/NamedPipeSecurity.h"
#include "Tests/TestHarness.h"

namespace
{
namespace pipes = winutil::pipes;

std::vector<ACCESS_ALLOWED_ACE*> allowedAces(pipes::PipeSecurity& security)
{
	std::vector<ACCESS_ALLOWED_ACE*> result;
	BOOL present = FALSE;
	BOOL defaulted = FALSE;
	PACL dacl = nullptr;
	if (!GetSecurityDescriptorDacl(security.attributes()->lpSecurityDescriptor, &present, &dacl, &defaulted) || !present || dacl == nullptr)
		return result;
	for (DWORD i = 0; i < dacl->AceCount; i++)
	{
		void* ace = nullptr;
		if (GetAce(dacl, i, &ace) && static_cast<ACE_HEADER*>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE)
			result.push_back(static_cast<ACCESS_ALLOWED_ACE*>(ace));
	}
	return result;
}

bool aceIsFor(const ACCESS_ALLOWED_ACE* ace, WELL_KNOWN_SID_TYPE type)
{
	BYTE sid[SECURITY_MAX_SID_SIZE] = {};
	DWORD size = sizeof(sid);
	return CreateWellKnownSid(type, nullptr, sid, &size) && EqualSid(const_cast<DWORD*>(&ace->SidStart), sid);
}

void testDeviceTestDescriptorGrantsLocalServiceOnlyAWrite(test::Harness& harness)
{
	pipes::PipeSecurity security(pipes::kDeviceTestServerSddl);
	harness.require(security.valid(), "the device test pipe's SDDL parses");
	const std::vector<ACCESS_ALLOWED_ACE*> aces = allowedAces(security);
	harness.expectEqual(aces.size(), size_t(3), "SYSTEM, Administrators and LOCAL SERVICE, nobody else");

	const ACCESS_ALLOWED_ACE* localService = nullptr;
	for (const ACCESS_ALLOWED_ACE* ace : aces)
	{
		if (aceIsFor(ace, WinLocalServiceSid))
			localService = ace;
	}
	harness.require(localService != nullptr, "audiodg's LOCAL SERVICE account has an entry");
	harness.expect((localService->Mask & FILE_WRITE_DATA) != 0, "LOCAL SERVICE may write a message");
	harness.expect((localService->Mask & FILE_CREATE_PIPE_INSTANCE) == 0, "LOCAL SERVICE may not add an instance of the pipe");
	harness.expect((localService->Mask & (WRITE_DAC | WRITE_OWNER)) == 0, "LOCAL SERVICE may not change who else may");
}

void testControlPipeDescriptorNamesTheUserAndSystem(test::Harness& harness)
{
	const std::wstring sid = pipes::currentUserSid();
	harness.require(!sid.empty(), "the test process can read its own user SID");
	pipes::PipeSecurity security(pipes::userOnlySddl(sid));
	harness.require(security.valid(), "the control pipe's SDDL parses");
	const std::vector<ACCESS_ALLOWED_ACE*> aces = allowedAces(security);
	harness.expectEqual(aces.size(), size_t(2), "the user and SYSTEM, nobody else");

	PSID userSid = nullptr;
	harness.require(ConvertStringSidToSidW(sid.c_str(), &userSid) != FALSE, "the SID text converts back");
	bool userFound = false;
	bool systemFound = false;
	for (const ACCESS_ALLOWED_ACE* ace : aces)
	{
		userFound = userFound || EqualSid(const_cast<DWORD*>(&ace->SidStart), userSid);
		systemFound = systemFound || aceIsFor(ace, WinLocalSystemSid);
	}
	LocalFree(userSid);
	harness.expect(userFound && systemFound, "the entries are for the current user and for SYSTEM");
}

// The current user stands in for LOCAL SERVICE: the pipe grants it exactly
// the LOCAL SERVICE mask of the device test descriptor and nothing else.
void testTheNarrowGrantIsEnoughToSendAndNoMore(test::Harness& harness)
{
	const std::wstring name = L"\\\\.\\pipe\\EngineOrchestrationTests-narrow-" + std::to_wstring(GetCurrentProcessId());
	pipes::PipeSecurity security(L"D:P(A;;0x12019b;;;" + pipes::currentUserSid() + L")");
	harness.require(security.valid(), "the narrow test descriptor parses");

	winutil::UniqueHandle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES,
		0, 1024, 0, security.attributes()));
	harness.require(static_cast<bool>(server), "the server instance is created");

	winutil::UniqueHandle second(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 0, 1024, 0, nullptr));
	const DWORD secondError = GetLastError();
	harness.expect(!second && secondError == ERROR_ACCESS_DENIED,
		"an account with the narrow grant cannot add an instance that would receive the messages");

	std::string received;
	std::atomic<bool> readerDone = false;
	std::thread reader([&] {
		const bool connected = ConnectNamedPipe(server.get(), nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
		char buffer[64] = {};
		DWORD read = 0;
		if (connected && ReadFile(server.get(), buffer, sizeof(buffer), &read, nullptr))
			received.assign(buffer, read);
		readerDone = true;
	});

	winutil::UniqueHandle client = pipes::openDeviceTestClient(name);
	const bool opened = static_cast<bool>(client);
	harness.expect(opened, "the device test client opens the pipe with FILE_WRITE_DATA");
	if (opened)
	{
		DWORD written = 0;
		harness.expect(WriteFile(client.get(), "hello", 5, &written, nullptr) && written == 5, "and writes its message");
		harness.expect(FlushFileBuffers(client.get()) != FALSE, "and FlushFileBuffers works without GENERIC_WRITE");
	}
	client.reset();
	// Without a client the reader is still waiting in ConnectNamedPipe.
	while (!opened && !readerDone)
	{
		CancelSynchronousIo(reader.native_handle());
		Sleep(10);
	}
	reader.join();
	harness.expect(received == "hello", "the server reads the message");
}

void testTheFirstInstanceFlagFindsATakenName(test::Harness& harness)
{
	const std::wstring name = L"\\\\.\\pipe\\EngineOrchestrationTests-taken-" + std::to_wstring(GetCurrentProcessId());
	winutil::UniqueHandle squatter(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 0, 1024, 0, nullptr));
	harness.require(static_cast<bool>(squatter), "another program's instance is created first");

	winutil::UniqueHandle ours(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 0, 1024, 0, nullptr));
	const DWORD error = GetLastError();
	harness.expect(!ours && error == ERROR_ACCESS_DENIED,
		"FILE_FLAG_FIRST_PIPE_INSTANCE fails with ERROR_ACCESS_DENIED when the name already has an instance");
}
} // namespace

void runNamedPipeSecurityTests(test::Harness& harness)
{
	testDeviceTestDescriptorGrantsLocalServiceOnlyAWrite(harness);
	testControlPipeDescriptorNamesTheUserAndSystem(harness);
	testTheNarrowGrantIsEnoughToSendAndNoMore(harness);
	testTheFirstInstanceFlagFindsATakenName(harness);
}
