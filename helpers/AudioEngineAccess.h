/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	"Can audiodg.exe read this file?" - asked and answered in one place.

	That question is the most common failure in the field. audiodg.exe hosts every
	APO and runs as LOCAL SERVICE, so a file it cannot read is a filter that never
	loads. When Velopack installs the app under %LocalAppData%, the default ACL
	grants the installing user and Administrators and nobody else, and the symptom
	the user sees is not a permission error but IAudioClient::GetMixFormat or
	Initialize returning E_ACCESSDENIED for the device.

	Before this module the knowledge was spread over three languages and four
	places: RegistryHelper::getFileAccessForUser did the AuthZ access check (read
	only, six call sites in the Editor), ApoRegistration spawned icacls with the
	grant spelled out inline, tools/Diagnose-EqualizerAPO.ps1 re-derived the same
	check with Get-Acl, and tools/Repair-EqualizerAPO.ps1 re-derived the same grant
	with icacls again. The program could detect the condition and could also fix
	it, but no single path did both, so users were told to download a PowerShell
	script for a repair the app was already capable of.

	The SIDs and the permissions each one needs are declared here once, and both
	directions - the check and the grant - read them from the same table.

	Grants require elevation. isElevated() is exported so callers can say so
	instead of discovering it from a failure: ApoRegistration's hooks run elevated
	by contract, and the assertion there is what makes the contract visible.
*/

#pragma once

#include <string>

namespace AudioEngineAccess
{

// Result of a grant. PartlyApplied is real and worth reporting separately:
// icacls walks a tree and reports failures per file, so a locked file somewhere
// below the root leaves the rest correctly granted.
enum class Grant
{
	Applied,
	PartlyApplied,
	Failed,
	NotElevated
};

// True when this process can rewrite an ACL on a machine-wide path at all.
bool isElevated();

// The access mask LOCAL SERVICE would be granted on this path. Throws
// AccessQueryException when the path cannot be queried, which is not the same as
// "no access" and must not be reported as such.
unsigned long accessForAudioEngine(const std::wstring& path);
// The same for the BUILTIN\Users group, which is what a non-administrator
// running the Editor or Device Selector is covered by.
unsigned long accessForUsers(const std::wstring& path);

// The question the field asks. Answers true when the path cannot be queried:
// every caller uses this to decide whether to warn, and warning because a query
// failed would put a permission error on screen for a file that is fine.
bool isReadableByAudioEngine(const std::wstring& path);
// Whether a non-administrator can run what lives here.
bool isRunnableByUsers(const std::wstring& path);

// Grants LOCAL SERVICE and Users read+execute over the install tree, so audiodg
// can map EqualizerAPO.dll and a non-administrator can start the Editor.
Grant grantEngineAccess(const std::wstring& installRoot);
// Grants Users full control (they edit configs) and LOCAL SERVICE modify
// (audiodg reads configs and writes APO trace logs) over the config tree.
Grant grantConfigAccess(const std::wstring& configDir);

// A one-line, human-readable form of a Grant, for logs and the diagnostics
// report. Deliberately not translated: it goes into a log file a maintainer
// reads, not onto the screen.
const wchar_t* describe(Grant grant);

} // namespace AudioEngineAccess

// Thrown by the accessFor* functions when the path's security descriptor cannot
// be read. It is a separate type from RegistryException because this module is
// about files, and because a caller that catches it has to decide something
// different from "no access".
class AccessQueryException
{
public:
	explicit AccessQueryException(const std::wstring& message)
		: message(message) {}

	const std::wstring& getMessage() const
	{
		return message;
	}

private:
	std::wstring message;
};
