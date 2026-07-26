/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The install-path report, inside the program that performs the install.

	It used to live outside it. tools/Diagnose-EqualizerAPO.ps1 collected all of
	this in PowerShell - the install path, the COM registration, whether audiodg
	can read the tree, and which endpoints have our APOs attached - and to do it,
	it wrote the endpoint property GUIDs out a second time. A user with a broken
	install therefore had to be talked through downloading and running a script
	before anyone could see what was wrong, and the script's copy of the GUID
	table could drift from the program's without anything noticing.

	Everything here is read-only. Nothing in this file changes a machine, which is
	what makes it safe to run before deciding whether to change one, and why it
	does not require elevation.

	The output is a list of lines rather than a struct, because there is exactly
	one consumer shape: a block of text a user pastes into a bug report. Deciding
	what is worth printing is the whole content of this module, so a caller that
	got a struct would only have to re-derive the same judgements.

	Not translated, and deliberately: this is text a maintainer reads.
*/

#pragma once

#include <string>
#include <vector>

class IRegistry;

namespace InstallDiagnostics
{

// The whole report. Reads the live registry by default; a test passes a fake.
std::vector<std::wstring> collect();
std::vector<std::wstring> collect(const IRegistry& registry);

// Every endpoint that currently has one of our two APO CLSIDs in its chain, one
// line each: direction, device name, connection name, and which slots. Exposed
// on its own because it answers the single most common question ("is it actually
// attached to anything?") and because the report above is otherwise dominated by
// the ACL section.
std::vector<std::wstring> attachedEndpoints(const IRegistry& registry);

// Collects the report and puts it where a user can find it: a timestamped file
// next to the log files, and the console this process was started from if there
// was one. Returns the file's path, or an empty string when it could not be
// written.
//
// No Qt, on purpose. Both programs that offer --diagnose want it available before
// they have built anything: the Editor runs it before its Velopack runtime and
// its QApplication exist, and Device Selector before it has a window to parent a
// dialog to.
//
// A GUI-subsystem process has no stdout attached to the shell that started it,
// which is why this attaches to the parent console explicitly. Without that,
// running "Editor --diagnose" from PowerShell would print nothing at all and
// read as a switch that does not work.
std::wstring writeReport();

} // namespace InstallDiagnostics
