/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See AudioEngineAccess.h for why this module exists.

	The grants are applied by spawning the system icacls.exe rather than by
	building an ACL here. That is what ApoRegistration did before the code moved,
	and it is kept on purpose: icacls walks the tree, merges with the existing
	DACL, and reports per-file failures, all of which a hand-written
	SetNamedSecurityInfo loop would have to reproduce. This is the code path that
	decides whether a user's audio works at all, so it is a bad place to trade
	proven behaviour for elegance.

	Trust boundary, carried over with the code: the paths come from the local
	install location and the principals are built-in well-known SIDs, never from
	network or user input. If either ever becomes caller-supplied it has to be
	validated and quoted before it reaches a command line.
*/

#include "AudioEngineAccess.h"

#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <authz.h>
#include <sddl.h>

#include "LogHelper.h"
#include "Win32Resource.h"
#include "PathHelper.h"

namespace
{
// THE TABLE. Every principal this program cares about, the SID literal icacls
// takes, and the RID the AuthZ check needs. Both directions read from here, so a
// grant and the check that verifies it cannot drift apart.
//
// LOCAL SERVICE (S-1-5-19) is the account audiodg.exe runs under, and
// BUILTIN\Users (S-1-5-32-545) is what covers a non-administrator running the
// Editor or Device Selector.
constexpr wchar_t kLocalServiceSid[] = L"*S-1-5-19";
constexpr wchar_t kUsersSid[] = L"*S-1-5-32-545";

// icacls exit codes: 0 is complete success, and anything else means at least one
// file was refused. It prints the count of failures, which is why a non-zero code
// is reported as a partial application rather than as a total failure - the
// remaining files did get the grant.
constexpr int kIcaclsSuccess = 0;

std::wstring systemPath()
{
	wchar_t buffer[MAX_PATH];
	UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
	if (length == 0 || length > MAX_PATH)
		return L"C:\\Windows\\System32";
	return std::wstring(buffer, length);
}

// Audit #250 F018: the shared path vocabulary lives in PathHelper.h.
using pathutil::joinPath;
using pathutil::pathExists;

// Runs a system tool to completion and returns its exit code, or -1 when it
// could not be started or timed out. Moved here with the icacls calls it exists
// for; nothing else in the tree spawned a process through the old copy.
int runToCompletion(const std::wstring& executable, const std::wstring& arguments, unsigned timeoutMs)
{
	std::wstring commandLine = L"\"" + executable + L"\" " + arguments;
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');

	STARTUPINFOW startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE;

	winutil::UniqueProcessInformation processInfo;

	if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, processInfo.put()))
	{
		LogFStatic(L"[AudioEngineAccess] CreateProcess failed for %s (gle=%lu)", executable.c_str(), GetLastError());
		return -1;
	}

	DWORD waitResult = WaitForSingleObject(processInfo.process(), timeoutMs);
	DWORD exitCode = static_cast<DWORD>(-1);
	if (waitResult == WAIT_OBJECT_0)
	{
		GetExitCodeProcess(processInfo.process(), &exitCode);
	}
	else
	{
		LogFStatic(L"[AudioEngineAccess] %s timed out after %u ms", executable.c_str(), timeoutMs);
		TerminateProcess(processInfo.process(), 1);
	}

	return static_cast<int>(exitCode);
}

// The AuthZ access check, moved out of RegistryHelper. It was never a registry
// operation: it asks what a principal would be granted on a *file*, and its
// presence in RegistryHelper.h is part of why that header has to pull in the
// Win32 headers for every translation unit that wants to read a registry value.
// Sub-authorities of one well-known SID. Two are needed because the accounts
// this module asks about are not the same shape: LOCAL SERVICE is S-1-5-19, one
// sub-authority under NT AUTHORITY, while BUILTIN\Users is S-1-5-32-545, an alias
// inside the built-in domain. Building the second one with a single
// sub-authority would silently produce S-1-5-545, a SID that exists nowhere and
// would therefore be granted nothing - the check would report every path as
// closed to normal users.
struct WellKnownSid
{
	BYTE subAuthorityCount;
	DWORD first;
	DWORD second;
};

constexpr WellKnownSid kLocalService = {1, SECURITY_LOCAL_SERVICE_RID, 0};
constexpr WellKnownSid kUsers = {2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS};

unsigned long accessForSid(const std::wstring& path, const WellKnownSid& principal)
{
	winutil::UniqueLocalPtr<void> securityDescriptor;
	if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION
		| GROUP_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, securityDescriptor.put()) != ERROR_SUCCESS)
		throw AccessQueryException(L"Error in GetNamedSecurityInfoW for " + path);

	winutil::UniqueAuthzResourceManager manager;
	if (!AuthzInitializeResourceManager(AUTHZ_RM_FLAG_NO_AUDIT, nullptr, nullptr, nullptr, nullptr, manager.put()))
		throw AccessQueryException(L"Error in AuthzInitializeResourceManager for " + path);

	winutil::UniqueSid sid;
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&authority, principal.subAuthorityCount,
			principal.first, principal.second, 0, 0, 0, 0, 0, 0, sid.put()))
		throw AccessQueryException(L"Error in AllocateAndInitializeSid for " + path);

	LUID unusedId = {0};
	winutil::UniqueAuthzContext context;
	if (!AuthzInitializeContextFromSid(0, sid.get(), manager.get(), nullptr, unusedId, nullptr, context.put()))
		throw AccessQueryException(L"Error in AuthzInitializeContextFromSid for " + path);

	AUTHZ_ACCESS_REQUEST request = {0};
	request.DesiredAccess = MAXIMUM_ALLOWED;
	request.PrincipalSelfSid = nullptr;
	request.ObjectTypeList = nullptr;
	request.ObjectTypeListLength = 0;
	request.OptionalArguments = nullptr;

	AUTHZ_ACCESS_REPLY reply = {0};
	BYTE buffer[1024];
	RtlZeroMemory(buffer, sizeof(buffer));
	reply.ResultListLength = 1;
	reply.GrantedAccessMask = reinterpret_cast<ACCESS_MASK*>(buffer);
	reply.Error = reinterpret_cast<DWORD*>(buffer + sizeof(ACCESS_MASK));

	if (!AuthzAccessCheck(0, context.get(), &request, nullptr, securityDescriptor.get(), nullptr, 0, &reply, nullptr))
		throw AccessQueryException(L"Error in AuthzAccessCheck for " + path);

	return *reply.GrantedAccessMask;
}

// The two spellings of "may read this" a granted mask can carry. Every one of
// the six Editor call sites tested both, because a grant written as generic
// rights and a grant written as file-specific rights both mean the same thing
// here.
bool maskAllowsRead(unsigned long mask)
{
	return (mask & GENERIC_READ) == GENERIC_READ
		|| (mask & FILE_GENERIC_READ) == FILE_GENERIC_READ;
}

bool maskAllowsExecute(unsigned long mask)
{
	return (mask & GENERIC_EXECUTE) == GENERIC_EXECUTE
		|| (mask & FILE_GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE;
}

AudioEngineAccess::Grant applyGrant(const std::wstring& path, const std::wstring& grants)
{
	if (!AudioEngineAccess::isElevated())
		return AudioEngineAccess::Grant::NotElevated;
	if (!pathExists(path))
		return AudioEngineAccess::Grant::Failed;

	const std::wstring icacls = joinPath(systemPath(), L"icacls.exe");
	// /T recurses, /C keeps going past a file it cannot touch, /Q keeps the log
	// readable.
	const std::wstring arguments = L"\"" + path + L"\" " + grants + L" /T /C /Q";
	const int code = runToCompletion(icacls, arguments, 30000);
	if (code == kIcaclsSuccess)
		return AudioEngineAccess::Grant::Applied;
	if (code < 0)
		return AudioEngineAccess::Grant::Failed;
	return AudioEngineAccess::Grant::PartlyApplied;
}
} // namespace

namespace AudioEngineAccess
{

bool isElevated()
{
	winutil::UniqueHandle token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()))
		return false;

	TOKEN_ELEVATION elevation = {};
	DWORD returned = 0;
	if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &returned))
		return false;

	return elevation.TokenIsElevated != 0;
}

unsigned long accessForAudioEngine(const std::wstring& path)
{
	return accessForSid(path, kLocalService);
}

unsigned long accessForUsers(const std::wstring& path)
{
	return accessForSid(path, kUsers);
}

bool isReadableByAudioEngine(const std::wstring& path)
{
	try
	{
		return maskAllowsRead(accessForAudioEngine(path));
	}
	catch (const AccessQueryException&)
	{
		// A path whose descriptor cannot be read is not a path we know to be
		// unreadable, and every caller turns a false into a warning on screen.
		return true;
	}
}

bool isRunnableByUsers(const std::wstring& path)
{
	try
	{
		const unsigned long mask = accessForUsers(path);
		return maskAllowsRead(mask) && maskAllowsExecute(mask);
	}
	catch (const AccessQueryException&)
	{
		return true;
	}
}

Grant grantEngineAccess(const std::wstring& installRoot)
{
	// Read+execute for both: audiodg maps EqualizerAPO.dll out of this tree, and
	// a non-administrator has to be able to start the Editor from it.
	const std::wstring grants =
		L"/grant " + std::wstring(kLocalServiceSid) + L":(OI)(CI)RX "
		L"/grant " + std::wstring(kUsersSid) + L":(OI)(CI)RX";
	return applyGrant(installRoot, grants);
}

Grant grantConfigAccess(const std::wstring& configDir)
{
	// Modify for Users because this is the directory they edit configs in,
	// and for LOCAL SERVICE because audiodg both reads the configs and
	// writes the APO trace log next to them.
	//
	// Audit #250 F043: Users used to get F (full control), which includes
	// WRITE_DAC/WRITE_OWNER - any standard user could re-ACL the directory
	// and cut LOCAL SERVICE's read, silencing the whole APO. M covers every
	// operation config editing actually needs.
	const std::wstring grants =
		L"/grant " + std::wstring(kUsersSid) + L":(OI)(CI)M "
		L"/grant " + std::wstring(kLocalServiceSid) + L":(OI)(CI)M";
	return applyGrant(configDir, grants);
}

const wchar_t* describe(Grant grant)
{
	switch (grant)
	{
	case Grant::Applied:
		return L"applied";
	case Grant::PartlyApplied:
		return L"applied except for files that refused it";
	case Grant::NotElevated:
		return L"not attempted: the process is not elevated";
	case Grant::Failed:
		break;
	}
	return L"failed";
}

} // namespace AudioEngineAccess
