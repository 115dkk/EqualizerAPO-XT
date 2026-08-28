/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See RegistryTransaction.h for what this class promises and what it refuses.
*/

#include "stdafx.h"

#include "services/registry/RegistryTransaction.h"
#include "services/registry/WindowsRegistry.h"

using std::vector;
using std::wstring;

RegistryTransaction::RegistryTransaction(IRegistry& target)
	: target(target)
{
}

RegistryTransaction::~RegistryTransaction()
{
	// Not committed means the scope was left early, which for this class is the
	// definition of failure.
	rollback();
}

void RegistryTransaction::commit()
{
	committed = true;
	journal.clear();
}

void RegistryTransaction::rollback()
{
	// Reverse order is the whole point: a value written into a key this
	// transaction created has to go before the key itself does.
	for (vector<Entry>::const_reverse_iterator it = journal.rbegin(); it != journal.rend(); ++it)
		undo(*it);

	journal.clear();
}

size_t RegistryTransaction::pendingUndoCount() const
{
	return journal.size();
}

bool RegistryTransaction::isFullyReversible() const
{
	return fullyReversible;
}

const vector<wstring>& RegistryTransaction::rollbackFailures() const
{
	return failures;
}

const vector<wstring>& RegistryTransaction::appliedOperations() const
{
	return applied;
}

void RegistryTransaction::recordApplied(const wstring& description)
{
	applied.push_back(description);
}

// --- Reads.

wstring RegistryTransaction::readValue(const wstring& key, const wstring& valuename) const
{
	return target.readValue(key, valuename);
}

unsigned long RegistryTransaction::readDWORDValue(const wstring& key, const wstring& valuename) const
{
	return target.readDWORDValue(key, valuename);
}

vector<wstring> RegistryTransaction::readMultiValue(const wstring& key, const wstring& valuename) const
{
	return target.readMultiValue(key, valuename);
}

vector<unsigned char> RegistryTransaction::readBinaryValue(const wstring& key, const wstring& valuename) const
{
	return target.readBinaryValue(key, valuename);
}

vector<wstring> RegistryTransaction::enumSubKeys(const wstring& key) const
{
	return target.enumSubKeys(key);
}

vector<wstring> RegistryTransaction::enumValues(const wstring& key) const
{
	return target.enumValues(key);
}

bool RegistryTransaction::keyExists(const wstring& key) const
{
	return target.keyExists(key);
}

bool RegistryTransaction::valueExists(const wstring& key, const wstring& valuename) const
{
	return target.valueExists(key, valuename);
}

bool RegistryTransaction::keyEmpty(const wstring& key) const
{
	return target.keyEmpty(key);
}

// --- Writes. Each one journals first, so a snapshot that cannot be taken stops
// the operation before it changes anything.

void RegistryTransaction::writeValue(const wstring& key, const wstring& valuename, const wstring& value)
{
	const Entry entry = prepareValueWrite(key, valuename);
	target.writeValue(key, valuename, value);
	keep(entry);
	recordApplied(L"write " + key + L"\\" + valuename + L" = " + value);
}

void RegistryTransaction::writeDWORDValue(const wstring& key, const wstring& valuename, unsigned long value)
{
	const Entry entry = prepareValueWrite(key, valuename);
	target.writeDWORDValue(key, valuename, value);
	keep(entry);
	recordApplied(L"write dword " + key + L"\\" + valuename + L" = " + std::to_wstring(value));
}

void RegistryTransaction::writeMultiValue(const wstring& key, const wstring& valuename, const wstring& value)
{
	const Entry entry = prepareValueWrite(key, valuename);
	target.writeMultiValue(key, valuename, value);
	keep(entry);
	recordApplied(L"write multi " + key + L"\\" + valuename + L" = " + value);
}

void RegistryTransaction::writeMultiValue(const wstring& key, const wstring& valuename, const vector<wstring>& values)
{
	const Entry entry = prepareValueWrite(key, valuename);
	target.writeMultiValue(key, valuename, values);
	keep(entry);
	recordApplied(L"write multi " + key + L"\\" + valuename + L" ("
		+ std::to_wstring(values.size()) + L" strings)");
}

void RegistryTransaction::deleteValue(const wstring& key, const wstring& valuename)
{
	// A missing key or value makes the delete throw, and there is nothing to put
	// back in that case. keyExists first because valueExists throws for a missing
	// key rather than answering.
	Entry entry;
	if (target.keyExists(key) && target.valueExists(key, valuename))
	{
		entry.kind = Entry::Kind::RestoreValue;
		entry.key = key;
		entry.value = snapshot(key, valuename);
	}

	target.deleteValue(key, valuename);
	keep(entry);
	recordApplied(L"delete " + key + L"\\" + valuename);
}

void RegistryTransaction::createKey(const wstring& key)
{
	// createKey creates every missing level of the path, so undoing it means
	// deleting every level it brought into existence, and only those.
	vector<wstring> created;
	for (size_t position = key.find(L'\\', key.find(L'\\') + 1); position != wstring::npos;
		position = key.find(L'\\', position + 1))
	{
		const wstring prefix = key.substr(0, position);
		if (!target.keyExists(prefix))
			created.push_back(prefix);
	}
	if (!target.keyExists(key))
		created.push_back(key);

	target.createKey(key);

	// Nothing is recorded when the key was already there. createKey succeeds on an
	// existing key, and a report line saying a key was created when it was not
	// misleads the person reading it - which matters because that report is the
	// whole point of recording what was applied.
	if (!created.empty())
	{
		recordApplied(L"create key " + key);

		Entry entry;
		entry.kind = Entry::Kind::DeleteKeys;
		// Deepest first, because a parent cannot be deleted while a child is left.
		entry.keys.assign(created.rbegin(), created.rend());
		journal.push_back(entry);
	}
}

void RegistryTransaction::deleteKey(const wstring& key)
{
	Entry entry;
	if (target.keyExists(key))
	{
		entry.kind = Entry::Kind::RestoreKey;
		entry.key = key;
		// deleteKey only succeeds on a key without subkeys, so this key's own
		// values are the whole of what disappears with it.
		const vector<wstring> valueNames = target.enumValues(key);
		for (const wstring& valuename : valueNames)
			entry.values.push_back(snapshot(key, valuename));
	}

	target.deleteKey(key);
	keep(entry);
	recordApplied(L"delete key " + key);
}

void RegistryTransaction::takeOwnership(const wstring& key)
{
	fullyReversible = false;
	target.takeOwnership(key);
	recordApplied(L"take ownership of " + key + L" (not reversible)");
}

void RegistryTransaction::makeWritable(const wstring& key)
{
	fullyReversible = false;
	target.makeWritable(key);
	recordApplied(L"grant write access to " + key + L" (not reversible)");
}

void RegistryTransaction::saveToFile(const wstring& key, const vector<wstring>& valuenames, const wstring& filepath)
{
	target.saveToFile(key, valuenames, filepath);
	recordApplied(L"export " + std::to_wstring(valuenames.size()) + L" values of " + key
		+ L" to " + filepath);
}

// --- Journalling.

RegistryTransaction::ValueSnapshot RegistryTransaction::snapshot(const wstring& key, const wstring& valuename) const
{
	ValueSnapshot result;
	result.name = valuename;

	// Each read is type-checked by the port, so trying them in turn is how the
	// stored type is discovered. There is no cheaper way through this interface,
	// and it runs a handful of times per install.
	try
	{
		result.stringValue = target.readValue(key, valuename);
		result.type = ValueSnapshot::Type::String;
		return result;
	}
	catch (const RegistryError&)
	{
	}

	try
	{
		result.multiValue = target.readMultiValue(key, valuename);
		result.type = ValueSnapshot::Type::MultiString;
		return result;
	}
	catch (const RegistryError&)
	{
	}

	try
	{
		result.dwordValue = target.readDWORDValue(key, valuename);
		result.type = ValueSnapshot::Type::Dword;
		return result;
	}
	catch (const RegistryError&)
	{
	}

	// Refusing here is what keeps the promise: the caller has not applied
	// anything yet, so the transaction stays exactly reversible.
	throw RegistryError(L"Registry value " + key + L"\\" + valuename
		+ L" holds a type this transaction cannot restore, so the change was not applied");
}

RegistryTransaction::Entry RegistryTransaction::prepareValueWrite(const wstring& key, const wstring& valuename) const
{
	Entry entry;

	// A write into a key that does not exist throws and changes nothing, so there
	// is no undo to prepare for it.
	if (!target.keyExists(key))
		return entry;

	entry.key = key;
	if (target.valueExists(key, valuename))
	{
		entry.kind = Entry::Kind::RestoreValue;
		entry.value = snapshot(key, valuename);
	}
	else
	{
		entry.kind = Entry::Kind::DeleteValue;
		entry.value.name = valuename;
	}
	return entry;
}

void RegistryTransaction::keep(const Entry& entry)
{
	if (entry.kind != Entry::Kind::Nothing)
		journal.push_back(entry);
}

void RegistryTransaction::restoreValue(const wstring& key, const ValueSnapshot& stored)
{
	switch (stored.type)
	{
	case ValueSnapshot::Type::String:
		target.writeValue(key, stored.name, stored.stringValue);
		break;
	case ValueSnapshot::Type::Dword:
		target.writeDWORDValue(key, stored.name, stored.dwordValue);
		break;
	case ValueSnapshot::Type::MultiString:
		target.writeMultiValue(key, stored.name, stored.multiValue);
		break;
	}
}

void RegistryTransaction::undo(const Entry& entry)
{
	// Every step is guarded and every failure is recorded rather than thrown:
	// this runs from a destructor while another exception is on its way out, and
	// one unrestorable value must not stop the rest of the rollback.
	try
	{
		switch (entry.kind)
		{
		case Entry::Kind::Nothing:
			break;

		case Entry::Kind::DeleteValue:
			if (target.keyExists(entry.key) && target.valueExists(entry.key, entry.value.name))
				target.deleteValue(entry.key, entry.value.name);
			break;

		case Entry::Kind::RestoreValue:
			if (target.keyExists(entry.key))
				restoreValue(entry.key, entry.value);
			else
				failures.push_back(L"cannot restore " + entry.key + L"\\" + entry.value.name + L": the key is gone");
			break;

		case Entry::Kind::DeleteKeys:
			for (const wstring& key : entry.keys)
			{
				// Something else may have put a value or a subkey below a key we
				// created - Windows itself does that under FxProperties since 24H2
				// (issue #189). Leaving such a key is right: it is no longer only
				// ours to remove.
				if (target.keyExists(key) && target.keyEmpty(key))
					target.deleteKey(key);
			}
			break;

		case Entry::Kind::RestoreKey:
			target.createKey(entry.key);
			for (const ValueSnapshot& value : entry.values)
				restoreValue(entry.key, value);
			break;
		}
	}
	catch (const RegistryError& e)
	{
		failures.push_back(e.getMessage());
	}
}
