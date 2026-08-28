/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A registry port that remembers how to undo itself.

	WHY THIS EXISTS. Installing Equalizer APO on one audio endpoint performs
	around forty registry operations: it copies the driver's own APO GUIDs into
	our Child APOs key, exports them to a .reg backup, then writes our two CLSIDs
	into the endpoint's FxProperties last. Written as a straight line, a failure
	anywhere in the middle leaves an endpoint that is half connected - our SFX
	CLSID in place, MFX still the driver's - and the very next load() reports that
	device as installed, because finding either CLSID is what "installed" means.
	The user then has a device whose audio graph nobody, including this program,
	can describe. reinstall() had the worse version of the same problem: it was
	uninstall(); load(); install(), so a throw from the middle call left the device
	uninstalled with no indication that anything was meant to follow.

	The shape that fixes it was already in this repository, in the APO DLL's own
	DllRegisterServer, which puts both GUIDs back on every failure path. This
	class generalises that: mutations are applied immediately, and each one first
	records the operation that would put back what it displaced. rollback()
	replays that record in reverse. So either the whole change is in place, or the
	registry is as it was.

	HOW TO USE IT. Wrap it around the port, perform the change through it, and
	commit at the end. Anything that leaves the scope without a commit - a throw,
	an early return - rolls back in the destructor:

		RegistryTransaction plan(registry);
		plan.createKey(...);
		plan.writeValue(...);
		plan.commit();

	It is an IRegistry itself, so the code inside the scope reads exactly like the
	code that used the port directly, and reads pass straight through.

	WHAT IT CANNOT UNDO, stated plainly because a rollback that quietly skips
	something is worse than one that refuses:

	  * takeOwnership and makeWritable are not journalled. Restoring a security
	    descriptor means storing one, and this port deliberately deals in
	    standard types only. Both are called in one place - install()'s recovery
	    path after createKey was denied - and both only widen access for the
	    Administrators group, so leaving them applied does not leave the endpoint
	    in a state the code cannot describe. isFullyReversible() answers false
	    once one of them has run, and that is the honest answer.

	  * saveToFile writes a .reg backup and is not journalled either. The backup
	    is the artefact the user is told to keep; deleting it on rollback would
	    throw away the only copy of what the driver had.

	  * A value whose previous content cannot be read back - REG_BINARY, or any
	    type this port cannot write - is refused *before* the change is applied.
	    The operation throws RegistryError and the journal stays valid, which
	    keeps the all-or-nothing promise instead of quietly breaking it. No call
	    site in the device layer overwrites or deletes a binary value; the check
	    is there so that a future one fails loudly rather than silently.

	THE JOURNAL ONLY DESCRIBES CHANGES THAT HAPPENED. An operation that throws
	changed nothing, so it adds no undo step - the snapshot is taken first,
	because that is the only moment the old contents still exist, but the entry is
	kept only after the mutation succeeds. Getting this backwards is subtle and
	costly: a write refused by an ACL would leave an entry that puts the value
	back on top of itself, and that step would then be refused by the same ACL, so
	an ordinary failed write would report itself as a rollback that did not finish
	- which is the signal reserved for a device left in a state nothing describes.

	ROLLBACK ITSELF DOES NOT THROW. It runs from a destructor during exception
	propagation, where throwing would end the process, so a failed undo step is
	recorded in rollbackFailures() and the remaining steps still run. The device
	layer reports that list: it is the difference between "nothing happened" and
	"this endpoint needs a reboot before you try again".
*/

#pragma once

#include <string>
#include <vector>

#include "services/registry/IRegistry.h"

class RegistryTransaction final : public IRegistry
{
public:
	explicit RegistryTransaction(IRegistry& target);
	// Rolls back unless commit() or rollback() already ran.
	~RegistryTransaction() override;

	RegistryTransaction(const RegistryTransaction&) = delete;
	RegistryTransaction& operator=(const RegistryTransaction&) = delete;

	// Keeps everything applied and drops the journal.
	void commit();
	// Puts back what the journal describes, in reverse order. Safe to call twice;
	// the second call has nothing to do. Never throws.
	void rollback();

	// Number of journalled operations still pending, which is what a rollback
	// would perform. Zero after commit() or rollback().
	size_t pendingUndoCount() const;
	// False once an operation ran that the journal cannot describe, which today
	// means takeOwnership or makeWritable.
	bool isFullyReversible() const;
	// Undo steps rollback() could not perform, each as one human-readable line.
	const std::vector<std::wstring>& rollbackFailures() const;

	// Every mutation that was applied, in order, one human-readable line each.
	// This is what makes an install reportable: the record is written by the same
	// call that performs the change, so it cannot describe a different change than
	// the one that happened. Survives commit() and rollback(), because a caller
	// reporting a failure needs to say how far it got.
	const std::vector<std::wstring>& appliedOperations() const;

	// --- IRegistry. Reads forward unchanged.

	std::wstring readValue(const std::wstring& key, const std::wstring& valuename) const override;
	unsigned long readDWORDValue(const std::wstring& key, const std::wstring& valuename) const override;
	std::vector<std::wstring> readMultiValue(const std::wstring& key, const std::wstring& valuename) const override;
	std::vector<unsigned char> readBinaryValue(const std::wstring& key, const std::wstring& valuename) const override;
	std::vector<std::wstring> enumSubKeys(const std::wstring& key) const override;
	std::vector<std::wstring> enumValues(const std::wstring& key) const override;
	bool keyExists(const std::wstring& key) const override;
	bool valueExists(const std::wstring& key, const std::wstring& valuename) const override;
	bool keyEmpty(const std::wstring& key) const override;

	void writeValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override;
	void writeDWORDValue(const std::wstring& key, const std::wstring& valuename, unsigned long value) override;
	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override;
	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& values) override;
	void deleteValue(const std::wstring& key, const std::wstring& valuename) override;

	void createKey(const std::wstring& key) override;
	void deleteKey(const std::wstring& key) override;

	void takeOwnership(const std::wstring& key) override;
	void makeWritable(const std::wstring& key) override;

	void saveToFile(const std::wstring& key, const std::vector<std::wstring>& valuenames, const std::wstring& filepath) override;

private:
	// One value as it was before we touched it. The type is kept because putting
	// a REG_DWORD back as a REG_SZ would be a different registry, not a restored
	// one.
	struct ValueSnapshot
	{
		enum class Type
		{
			String,
			Dword,
			MultiString
		};

		std::wstring name;
		Type type = Type::String;
		std::wstring stringValue;
		unsigned long dwordValue = 0;
		std::vector<std::wstring> multiValue;
	};

	struct Entry
	{
		enum class Kind
		{
			// Nothing to undo. Produced when the operation was going to throw
			// anyway, so that the journal never grows an entry for a change that
			// did not happen.
			Nothing,
			// The value was not there before: undo by deleting it.
			DeleteValue,
			// The value was there: undo by writing the snapshot back.
			RestoreValue,
			// These keys did not exist before: undo by deleting them, deepest first.
			DeleteKeys,
			// This key existed with these values: undo by creating and refilling it.
			RestoreKey
		};

		Kind kind = Kind::Nothing;
		std::wstring key;
		ValueSnapshot value;
		std::vector<std::wstring> keys;
		std::vector<ValueSnapshot> values;
	};

	// Reads the value so it can be put back. Throws RegistryError when its
	// type cannot be restored, which the caller lets propagate before applying.
	ValueSnapshot snapshot(const std::wstring& key, const std::wstring& valuename) const;
	// Builds the entry that would undo a write to this value, whatever it holds
	// now. Reads the current contents, so it has to run before the write - and it
	// is also where a value that cannot be restored is refused, which is why it
	// runs before anything is applied.
	Entry prepareValueWrite(const std::wstring& key, const std::wstring& valuename) const;
	// Adds a prepared entry to the journal, which happens only after the mutation
	// it undoes has actually succeeded. A write that threw changed nothing, and an
	// undo entry for it would make the rollback put a value back on top of itself
	// - harmless in the registry, but it turns a write that was refused into a
	// rollback step that is refused too, and a rollback failure means something
	// much worse than a write failure.
	void keep(const Entry& entry);
	void recordApplied(const std::wstring& description);
	void undo(const Entry& entry);
	void restoreValue(const std::wstring& key, const ValueSnapshot& stored);

	IRegistry& target;
	std::vector<Entry> journal;
	std::vector<std::wstring> applied;
	std::vector<std::wstring> failures;
	bool committed = false;
	bool fullyReversible = true;
};
