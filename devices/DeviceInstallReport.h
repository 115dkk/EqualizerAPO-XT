/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	What one install, uninstall or repair of a device actually did.

	Until this existed, the whole install path was silent. devices/,
	WindowsRegistry, WindowsServiceControl and DeviceSelector.cpp came to about 950 lines
	with not one log call between them, so a user reporting "Device Selector said
	access denied" left nothing behind to read: the exception message went into a
	message box that was already closed, and Editor.log had nothing because the
	program that performed the install never wrote to it.

	The knowledge had migrated out of the program instead. tools/
	Diagnose-EqualizerAPO.ps1 re-derives the endpoint layout in PowerShell, with
	the property GUIDs written out a second time, because that was the only way to
	see what an install had left on a machine.

	So the operations return their result as a value. Three groups, in the order a
	reader needs them:

	  * What was found. Whether the driver had published an FxProperties key at
	    all, which slots it had filled, whether the original chain was exported,
	    and which mode was requested. This is the part that explains why the rest
	    happened.

	  * What was written. One line per registry mutation, in order, taken from the
	    transaction that applied them - so the record cannot drift from the change.

	  * Why it stopped, if it did. The exception message, which carries the Win32
	    status the registry reported, plus any rollback step that could not run.
	    A non-empty rollbackFailures is the one case where a device can be left in
	    a state that neither "installed" nor "uninstalled" describes, and it is
	    reported loudly for that reason.

	The report is a plain value with no Qt in it, because the same shape is used
	by DeviceSelector's dialog, by the Velopack hooks that run without a GUI, and
	by the log file.
*/

#pragma once

#include <string>
#include <vector>

struct DeviceInstallReport
{
	enum class Operation
	{
		None,
		Install,
		Uninstall,
		Reinstall
	};

	enum class Outcome
	{
		// No operation has run on this object yet. The Voicemeeter and preview
		// device types never leave this state, because what they change is a
		// shortcut file rather than an endpoint's APO chain.
		NotAttempted,
		Succeeded,
		Failed
	};

	Operation operation = Operation::None;
	Outcome outcome = Outcome::NotAttempted;

	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	bool input = false;

	// --- What was found before anything was written.

	// False means the audio driver never published an FxProperties key, so
	// installing has to create one - the case the Editor calls experimental.
	bool fxPropertiesExisted = false;
	// One entry per slot the driver had filled, as "SFX = {guid}". Empty when the
	// endpoint had no APOs of its own, which is the common case for generic
	// hardware.
	std::vector<std::wstring> driverSlots;
	// Where the driver's own chain was exported to, empty when there was nothing
	// to export. This is the file a user needs to put the device back by hand.
	std::wstring backupPath;
	// "LFX/GFX", "SFX/MFX" or "SFX/EFX", as requested.
	std::wstring requestedMode;
	bool installPreMix = false;
	bool installPostMix = false;

	// --- What was written, in order.

	std::vector<std::wstring> appliedOperations;
	// True when the endpoint key's permissions had to be widened, which a
	// rollback cannot undo. Worth reporting on success too: it means this machine
	// has a driver that locks its own FxProperties key.
	bool permissionsWidened = false;

	// --- Why it stopped.

	std::wstring failure;
	std::vector<std::wstring> rollbackFailures;

	// The whole report as lines for a log file or the diagnostics output. Not
	// translated: this is text a maintainer reads in a bug report, and a
	// translated log is one nobody outside that language can act on.
	std::vector<std::wstring> toLines() const;

	// One line, for the case where the caller wants a summary rather than a
	// block: the operation, the device, and either "ok" or the failure.
	std::wstring toSummaryLine() const;

	// True when the operation left the endpoint in a state neither install nor
	// uninstall describes. Only a rollback that could not finish can do that.
	bool leftInconsistent() const
	{
		return !rollbackFailures.empty();
	}
};
