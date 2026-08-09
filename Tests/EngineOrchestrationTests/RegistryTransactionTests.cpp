/*
	This file is part of EqualizerAPO-XT.

	Unit tests for RegistryTransaction, the journalled port the device installer
	now runs inside.

	These are the tests that make the install rework judgeable at all. An install
	that fails halfway cannot be staged on a real machine - the failures it has to
	survive are ACLs on one driver-owned property, or another process holding the
	endpoint key - so the fake registry arms the failure and the transaction is
	asked what it left behind. Every test therefore checks the state *after* the
	rollback, not the rollback's own bookkeeping: what matters is whether the
	registry reads as it did before, and only that.
*/

#include <string>
#include <vector>

#include "services/registry/RegistryHelper.h"
#include "services/registry/RegistryTransaction.h"
#include "Tests/TestHarness.h"

#include "FakeRegistry.h"

namespace
{
using test::FakeRegistry;

const std::wstring rootKey = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO";
const std::wstring branchKey = rootKey + L"\\Transaction Test";
const std::wstring leafKey = branchKey + L"\\Leaf";

void testCommitKeepsEverythingAndEmptiesTheJournal(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);

	{
		RegistryTransaction plan(registry);
		plan.createKey(leafKey);
		plan.writeValue(leafKey, L"Name", L"value");
		harness.expectEqual(plan.pendingUndoCount(), size_t(2),
			"two operations that changed something are two operations a rollback would have to undo");
		harness.expectEqual(plan.appliedOperations().size(), size_t(2),
			"and two lines in the record of what was applied");

		// createKey succeeds on a key that is already there, and that is not a
		// change: the record has to stay silent about it or the install report
		// claims a key was created when it was not.
		plan.createKey(leafKey);
		harness.expectEqual(plan.appliedOperations().size(), size_t(2),
			"creating a key that already exists adds nothing to the record");
		harness.expectEqual(plan.pendingUndoCount(), size_t(2),
			"and nothing to undo");
		plan.commit();
		harness.expectEqual(plan.pendingUndoCount(), size_t(0),
			"a committed transaction has nothing left to undo, so its destructor does nothing");
	}

	harness.expect(registry.keyExists(leafKey),
		"the key a committed transaction created is still there after the transaction leaves scope");
	harness.expect(registry.readValue(leafKey, L"Name") == L"value",
		"the value a committed transaction wrote survives too");
}

void testLeavingScopeWithoutCommitUndoesEveryStep(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);
	registry.seedString(rootKey, L"Kept", L"original");

	{
		RegistryTransaction plan(registry);
		plan.createKey(leafKey);
		plan.writeValue(leafKey, L"New", L"written");
		plan.writeValue(rootKey, L"Kept", L"overwritten");
		// No commit: this is the shape of a throw escaping the install.
	}

	harness.expectFalse(registry.keyExists(leafKey),
		"a key the transaction created is gone again, and so is the branch key it had to create on the way");
	harness.expectFalse(registry.keyExists(branchKey),
		"createKey creates every missing level, so a rollback removes every level it created");
	harness.expect(registry.keyExists(rootKey),
		"a key that was already there is not ours to remove");
	harness.expect(registry.readValue(rootKey, L"Kept") == L"original",
		"an overwritten value goes back to what it held, which is the difference between a rollback and a cleanup");
}

void testRollbackRestoresValueTypesRatherThanJustText(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);
	registry.seedDword(rootKey, L"Flag", 7);
	registry.seedMulti(rootKey, L"Modes", {L"{first}", L"{second}"});

	{
		RegistryTransaction plan(registry);
		// The install writes REG_SZ into slots the driver may have filled with
		// something else, so the type has to come back as well as the contents.
		plan.writeValue(rootKey, L"Flag", L"not a dword any more");
		plan.deleteValue(rootKey, L"Modes");
	}

	harness.expectEqual(registry.readDWORDValue(rootKey, L"Flag"), 7ul,
		"a REG_DWORD comes back as a REG_DWORD; restoring it as text would leave a value the audio engine cannot read");
	const std::vector<std::wstring> modes = registry.readMultiValue(rootKey, L"Modes");
	harness.requireEqual(modes.size(), size_t(2),
		"a deleted REG_MULTI_SZ is restored with all of its strings, not just the first");
	harness.expect(modes[0] == L"{first}" && modes[1] == L"{second}",
		"the strings come back in order and verbatim");
}

void testRollbackRebuildsADeletedKeyWithItsValues(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedString(leafKey, L"Version", L"2");
	registry.seedDword(leafKey, L"Count", 3);

	{
		RegistryTransaction plan(registry);
		plan.deleteKey(leafKey);
		harness.expectFalse(registry.keyExists(leafKey),
			"the delete is applied immediately; the journal is for undoing it, not for deferring it");
	}

	harness.require(registry.keyExists(leafKey),
		"uninstall deletes the whole per-device key, so a rollback has to be able to put a key back");
	harness.expect(registry.readValue(leafKey, L"Version") == L"2",
		"the values that went with the key come back with it");
	harness.expectEqual(registry.readDWORDValue(leafKey, L"Count"), 3ul,
		"including their types");
}

void testAValueItCannotRestoreIsRefusedBeforeAnythingChanges(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);
	registry.seedBinary(rootKey, L"Format", {1, 2, 3, 4});

	bool refused = false;
	{
		RegistryTransaction plan(registry);
		try
		{
			plan.writeValue(rootKey, L"Format", L"text");
		}
		catch (const RegistryException&)
		{
			refused = true;
		}
	}

	harness.expect(refused,
		"a value whose previous contents cannot be read back is refused, because applying it would silently break the all-or-nothing promise");
	const std::vector<unsigned char> format = registry.readBinaryValue(rootKey, L"Format");
	harness.requireEqual(format.size(), size_t(4),
		"the refusal happens before the write, so the value is untouched");
	harness.expect(format[0] == 1 && format[3] == 4,
		"and its contents are exactly what they were");
}

void testRollbackLeavesAKeySomethingElseMovedInto(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);

	{
		RegistryTransaction plan(registry);
		plan.createKey(leafKey);
		// Windows does exactly this below FxProperties since 24H2 (issue #189):
		// something that is not ours appears under a key we created.
		registry.seedKey(leafKey + L"\\Foreign");
	}

	harness.expect(registry.keyExists(leafKey),
		"a key we created but that now holds someone else's subkey is no longer only ours to remove");
	harness.expect(registry.keyExists(leafKey + L"\\Foreign"),
		"and the foreign subkey survives, which is the point of not deleting the parent");
}

void testAFailedUndoStepIsRecordedAndTheRestStillRuns(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);
	registry.seedString(rootKey, L"First", L"original");
	registry.seedString(leafKey, L"Second", L"original");

	RegistryTransaction plan(registry);
	// Written in this order so the rollback, which runs in reverse, hits the
	// impossible step first and the recoverable one after it.
	plan.writeValue(rootKey, L"First", L"overwritten");
	plan.writeValue(leafKey, L"Second", L"overwritten");
	// Take the key out from under the pending undo, the way another process
	// deleting an endpoint key mid-install would.
	registry.deleteKey(leafKey);

	plan.rollback();

	harness.expectEqual(plan.rollbackFailures().size(), size_t(1),
		"an undo step that cannot run is recorded rather than thrown, because rollback runs while another exception is already on its way out");
	harness.expect(registry.readValue(rootKey, L"First") == L"original",
		"the steps that can still run do run; one unrestorable value must not abandon the rest of the rollback");
}

void testAWriteThatWasRefusedLeavesNoUndoStep(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);
	registry.seedString(rootKey, L"Locked", L"original");
	registry.failValueWrite(rootKey, L"Locked");

	RegistryTransaction plan(registry);
	bool threw = false;
	try
	{
		plan.writeValue(rootKey, L"Locked", L"attempted");
	}
	catch (const RegistryException&)
	{
		threw = true;
	}

	harness.expect(threw, "the refused write reports itself");
	harness.expectEqual(plan.pendingUndoCount(), size_t(0),
		"a write that changed nothing leaves nothing to undo; journalling it before applying it would put the value back on top of itself");

	plan.rollback();
	harness.expectEqual(plan.rollbackFailures().size(), size_t(0),
		"and so the rollback reports no failure - which matters because a refused write is ordinary while a refused rollback means the device is in a state nothing describes");
	harness.expect(registry.readValue(rootKey, L"Locked") == L"original",
		"the value is what it was");
}

void testPermissionChangesAreReportedAsIrreversible(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(rootKey);

	RegistryTransaction plan(registry);
	harness.expect(plan.isFullyReversible(),
		"a transaction that has only written values can be taken back completely");
	plan.takeOwnership(rootKey);
	harness.expectFalse(plan.isFullyReversible(),
		"a security descriptor is not something this port can store and put back, so the transaction says so instead of pretending");
	plan.commit();
}
} // namespace

void runRegistryTransactionTests(test::Harness& harness)
{
	testCommitKeepsEverythingAndEmptiesTheJournal(harness);
	testLeavingScopeWithoutCommitUndoesEveryStep(harness);
	testRollbackRestoresValueTypesRatherThanJustText(harness);
	testRollbackRebuildsADeletedKeyWithItsValues(harness);
	testAValueItCannotRestoreIsRefusedBeforeAnythingChanges(harness);
	testRollbackLeavesAKeySomethingElseMovedInto(harness);
	testAFailedUndoStepIsRecordedAndTheRestStillRuns(harness);
	testAWriteThatWasRefusedLeavesNoUndoStep(harness);
	testPermissionChangesAreReportedAsIrreversible(harness);
}
