/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The ASIO device record and its registry vocabulary over the fake
	registry: target enumeration from HKLM\SOFTWARE\ASIO, the derived
	wrapper CLSID, the entry and class trees in both registry views, the
	playback/capture records sharing one wrapper record, and the facts the
	engine host publishes. Nothing here touches the machine's registry, and
	the install directory the records point at is a folder of the test's own.
*/

#include <string>
#include <vector>

#include "asio/AsioRegistration.h"
#include "asio/StreamFacts.h"
#include "asio/WrapperRecord.h"
#include "devices/AsioAPOInfo.h"
#include "devices/DeviceException.h"
#include "runtime/errors/WideError.h"
#include "services/registry/RegistryPaths.h"
#include "Tests/FakeRegistry.h"
#include "Tests/TestDirectory.h"
#include "Tests/TestHarness.h"

using eapo::asio::AsioTarget;
namespace AsioRegistration = eapo::asio::AsioRegistration;

namespace
{
	test::Harness harness("DeviceRecordTests");

	const wchar_t* const toppingClsid = L"{6D241B5E-CF73-4043-A85F-EF11D4670955}";
	const wchar_t* const minidspClsid = L"{466A3ACF-0324-46F9-9A38-FB08FFDD208E}";

	// The InstallPath the records see. Whether the x86 wrapper ships is a
	// file test under it, so it has to be a folder this test owns: on a
	// machine with the product installed, C:\Program Files\EqualizerAPO
	// answered differently (audit #348 TD-73).
	test::TestDirectory& installDirectory()
	{
		static test::TestDirectory directory(L"DeviceRecordTests");
		return directory;
	}

	void seedTargets(test::FakeRegistry& registry)
	{
		const std::wstring root = AsioRegistration::asioRoot(false);
		registry.seedKey(root + L"\\Topping USB Audio Device");
		registry.seedString(root + L"\\Topping USB Audio Device", L"CLSID", toppingClsid);
		registry.seedString(root + L"\\Topping USB Audio Device", L"Description", L"Topping USB Audio Device");
		registry.seedKey(root + L"\\miniDSP ASIO Driver");
		registry.seedString(root + L"\\miniDSP ASIO Driver", L"CLSID", minidspClsid);
		// A stray subkey without a CLSID (some installers leave one) is skipped.
		registry.seedKey(root + L"\\Broken Driver");
		registry.seedKey(APP_REGPATH);
		registry.seedString(APP_REGPATH, L"InstallPath", installDirectory().path());
	}

	void testEnumerationAndDerivedIds()
	{
		test::FakeRegistry empty;
		harness.expect(AsioRegistration::enumerateTargets(empty).empty(), "no ASIO key: no targets, no throw");

		test::FakeRegistry registry;
		seedTargets(registry);
		std::vector<AsioTarget> targets = AsioRegistration::enumerateTargets(registry);
		harness.requireEqual(targets.size(), static_cast<size_t>(2), "two targets with a CLSID");
		harness.expect(targets[0].name == L"Topping USB Audio Device" || targets[1].name == L"Topping USB Audio Device", "the Topping target is listed");
		bool descriptionFallsBack = false;
		for (const AsioTarget& target : targets)
		{
			if (target.name == L"miniDSP ASIO Driver")
				descriptionFallsBack = target.description == L"miniDSP ASIO Driver";
		}
		harness.expect(descriptionFallsBack, "a target without Description uses its name");

		const std::wstring wrapper = AsioRegistration::wrapperClsidFor(toppingClsid);
		harness.expectEqual(wrapper.size(), static_cast<size_t>(38), "the wrapper CLSID is a braced GUID string");
		harness.expect(wrapper != toppingClsid, "the wrapper CLSID differs from the target's");
		harness.expect(wrapper == AsioRegistration::wrapperClsidFor(toppingClsid), "the derivation is deterministic");
		harness.expect(wrapper != AsioRegistration::wrapperClsidFor(minidspClsid), "different targets derive different wrappers");
		harness.expect(AsioRegistration::wrapperClsidFor(L"not a guid").empty(), "a malformed target CLSID derives nothing");
		harness.expect(AsioRegistration::wrapperClsidFor(L"Some.ProgID").empty(), "a ProgID is not taken for a CLSID");
		harness.expect(AsioRegistration::entryNameFor(L"Topping USB Audio Device") == L"Topping USB Audio Device (EQ APO XT)", "the entry name carries the suffix");
		harness.expectTrue(AsioRegistration::isWrapperEntry(L"X (EQ APO XT)"), "the suffix is recognized");
		harness.expectFalse(AsioRegistration::isWrapperEntry(L"Topping USB Audio Device"), "a target name is not a wrapper entry");
	}

	void testRegisterAndUnregisterBothViews()
	{
		test::FakeRegistry registry;
		seedTargets(registry);
		AsioTarget target;
		target.name = L"Topping USB Audio Device";
		target.clsid = toppingClsid;
		target.description = L"Topping USB Audio Device";
		const std::wstring wrapper = AsioRegistration::wrapperClsidFor(toppingClsid);

		AsioRegistration::registerWrapper(registry, target, L"C:\\eapo\\EqualizerAPOAsio.dll", L"C:\\eapo\\x86\\EqualizerAPOAsio.dll");
		const std::wstring entry64 = AsioRegistration::asioRoot(false) + L"\\Topping USB Audio Device (EQ APO XT)";
		const std::wstring entry32 = AsioRegistration::asioRoot(true) + L"\\Topping USB Audio Device (EQ APO XT)";
		harness.expect(registry.readValue(entry64, L"CLSID") == wrapper, "the 64-bit entry names the wrapper CLSID");
		harness.expect(registry.readValue(entry32, L"CLSID") == wrapper, "the 32-bit entry names the same CLSID");
		harness.expect(registry.readValue(entry64, L"Description") == L"Topping USB Audio Device (EQ APO XT)", "the description carries the suffix");
		harness.expect(registry.readValue(AsioRegistration::classesClsidRoot(false) + L"\\" + wrapper + L"\\InprocServer32", L"") == L"C:\\eapo\\EqualizerAPOAsio.dll", "the 64-bit class tree points at the 64-bit DLL");
		harness.expect(registry.readValue(AsioRegistration::classesClsidRoot(true) + L"\\" + wrapper + L"\\InprocServer32", L"") == L"C:\\eapo\\x86\\EqualizerAPOAsio.dll", "the WOW6432Node class tree points at the 32-bit DLL");
		harness.expect(registry.readValue(AsioRegistration::classesClsidRoot(false) + L"\\" + wrapper + L"\\InprocServer32", L"ThreadingModel") == L"Both", "ThreadingModel Both");
		harness.expectTrue(AsioRegistration::wrapperRegistered(registry, target), "wrapperRegistered sees the entry");
		harness.expectEqual(AsioRegistration::enumerateTargets(registry).size(), static_cast<size_t>(2), "the wrapper entry is not enumerated as a target");

		AsioRegistration::registerWrapper(registry, target, L"C:\\eapo\\EqualizerAPOAsio.dll", L"C:\\eapo\\x86\\EqualizerAPOAsio.dll");
		harness.expect(true, "registering twice is fine");

		AsioRegistration::unregisterWrapper(registry, target);
		harness.expectFalse(registry.keyExists(entry64), "the 64-bit entry is gone");
		harness.expectFalse(registry.keyExists(entry32), "the 32-bit entry is gone");
		harness.expectFalse(registry.keyExists(AsioRegistration::classesClsidRoot(false) + L"\\" + wrapper), "the 64-bit class tree is gone");
		harness.expectFalse(registry.keyExists(AsioRegistration::classesClsidRoot(true) + L"\\" + wrapper), "the WOW6432Node class tree is gone");
		AsioRegistration::unregisterWrapper(registry, target);
		harness.expect(true, "unregistering twice is fine");

		// Without a 32-bit DLL only the 64-bit view is written.
		AsioRegistration::registerWrapper(registry, target, L"C:\\eapo\\EqualizerAPOAsio.dll", L"");
		harness.expectTrue(registry.keyExists(entry64), "64-bit entry without a 32-bit DLL");
		harness.expectFalse(registry.keyExists(entry32), "no 32-bit entry without a 32-bit DLL");
		AsioRegistration::unregisterWrapper(registry, target);
	}

	// The second kind of record: a WASAPI endpoint as the target. The kind
	// and both endpoint GUIDs survive the registry, an old record without
	// the values reads as a driver record, and the endpoint target names its
	// entry after the device without characters a key cannot carry.
	void testWasapiRecordAndEndpointTarget()
	{
		test::FakeRegistry registry;
		const std::wstring endpoint = L"{A6974EEF-CBB1-4E81-B9CA-34B91FFF5279}";
		const AsioTarget target = AsioRegistration::endpointTarget(endpoint, L"Speakers", L"TOPPING USB DAC");
		harness.expect(target.name == L"TOPPING USB DAC - Speakers", "the entry base name is device - connection");
		harness.expect(target.clsid == endpoint, "the target CLSID is the endpoint GUID");
		harness.expect(AsioRegistration::endpointTarget(endpoint, L"Line\\Out", L"").name == L"Line/Out", "a backslash cannot name a key and becomes a slash");
		harness.expect(AsioRegistration::endpointTarget(endpoint, L"", L"Only device").name == L"Only device", "a missing connection name leaves the device name alone");
		harness.expect(AsioRegistration::entryNameFor(target.name) == L"TOPPING USB DAC - Speakers (EQ APO XT)", "the entry carries the suffix");
		harness.expect(!AsioRegistration::wrapperClsidFor(endpoint).empty() && AsioRegistration::wrapperClsidFor(endpoint) != endpoint, "the wrapper CLSID derives from the endpoint GUID");

		eapo::asio::WrapperRecord record;
		record.wrapperClsid = AsioRegistration::wrapperClsidFor(endpoint);
		record.targetKind = eapo::asio::TargetKind::WasapiExclusive;
		record.targetClsid = endpoint;
		record.targetName = target.name;
		record.renderEndpoint = endpoint;
		record.options.processOutput = true;
		record.options.processInput = false;
		eapo::asio::WrapperRecords::write(registry, record);
		eapo::asio::WrapperRecord back;
		harness.require(eapo::asio::WrapperRecords::read(registry, record.wrapperClsid, back), "the record reads back");
		harness.expect(back.targetKind == eapo::asio::TargetKind::WasapiExclusive, "the kind survives");
		harness.expect(back.renderEndpoint == endpoint, "the playback endpoint survives");
		harness.expect(back.captureEndpoint.empty(), "no recording endpoint was recorded");
		harness.expect(back.targetClsid == endpoint && back.targetName == target.name, "CLSID and name survive");
		harness.expectEqual(registry.readDWORDValue(eapo::asio::WrapperRecords::recordKey(record.wrapperClsid), L"TargetKind"), 1ul, "TargetKind 1 is the WASAPI kind");

		// A record written before the kind existed.
		const std::wstring oldKey = eapo::asio::WrapperRecords::recordKey(L"{0BAD0BAD-0000-4000-8000-000000000001}");
		registry.seedString(oldKey, L"TargetClsid", L"{6D241B5E-CF73-4043-A85F-EF11D4670955}");
		eapo::asio::WrapperRecord old;
		harness.require(eapo::asio::WrapperRecords::read(registry, L"{0BAD0BAD-0000-4000-8000-000000000001}", old), "an old record reads");
		harness.expect(old.targetKind == eapo::asio::TargetKind::AsioDriver, "and is a driver record");
		harness.expect(old.renderEndpoint.empty() && old.captureEndpoint.empty(), "with no endpoints");
	}

	// Audit #348 TD-08: an endpoint entry is named after the device's
	// friendly name at install time. Removing it after the device was renamed
	// used to derive the new name, miss the old key, and leave the entry in
	// every DAW's list. Removal now follows the wrapper CLSID.
	void testUnregisterFindsAnEntryUnderItsOldName()
	{
		test::FakeRegistry registry;
		const std::wstring endpoint = L"{A6974EEF-CBB1-4E81-B9CA-34B91FFF5279}";
		const AsioTarget installed = AsioRegistration::endpointTarget(endpoint, L"Speakers", L"TOPPING USB DAC");
		AsioRegistration::registerWrapper(registry, installed, L"C:\\eapo\\EqualizerAPOAsio.dll", L"C:\\eapo\\x86\\EqualizerAPOAsio.dll");
		harness.require(AsioRegistration::wrapperRegistered(registry, installed), "the entry is registered under the install-time name");

		// An unrelated driver entry must survive the sweep.
		registry.seedString(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\Other Driver", L"CLSID", L"{11111111-2222-3333-4444-555555555555}");

		const AsioTarget renamed = AsioRegistration::endpointTarget(endpoint, L"Speakers", L"Desk DAC");
		AsioRegistration::unregisterWrapper(registry, renamed);

		const std::wstring wrapperClsid = AsioRegistration::wrapperClsidFor(endpoint);
		for (int view = 0; view < 2; view++)
		{
			const std::wstring root = AsioRegistration::asioRoot(view == 1);
			bool left = false;
			if (registry.keyExists(root))
				for (const std::wstring& name : registry.enumSubKeys(root))
				{
					const std::wstring key = root + L"\\" + name;
					if (registry.valueExists(key, L"CLSID") && _wcsicmp(registry.readValue(key, L"CLSID").c_str(), wrapperClsid.c_str()) == 0)
						left = true;
				}
			harness.expect(!left, view == 0 ? "no 64-bit ASIO entry points at the wrapper after removal under a new name"
				: "no 32-bit ASIO entry points at the wrapper after removal under a new name");
		}
		harness.expect(registry.keyExists(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\Other Driver"), "another driver's entry is left alone");
	}

	void testRecordsShareOneWrapperRecord()
	{
		test::FakeRegistry registry;
		seedTargets(registry);
		std::vector<std::shared_ptr<AbstractAPOInfo>> playback, capture;
		AsioAPOInfo::appendInfos(playback, false, registry);
		AsioAPOInfo::appendInfos(capture, true, registry);
		harness.requireEqual(playback.size(), static_cast<size_t>(2), "a playback record per target");
		harness.requireEqual(capture.size(), static_cast<size_t>(2), "a capture record per target");

		std::shared_ptr<AbstractAPOInfo> out, in;
		for (const std::shared_ptr<AbstractAPOInfo>& info : playback)
		{
			if (info->getDeviceName() == L"Topping USB Audio Device")
				out = info;
		}
		for (const std::shared_ptr<AbstractAPOInfo>& info : capture)
		{
			if (info->getDeviceName() == L"Topping USB Audio Device")
				in = info;
		}
		harness.require(out != nullptr && in != nullptr, "both Topping records exist");
		harness.expect(out->getConnectionName() == L"ASIO", "the connection name is ASIO");
		harness.expect(out->getDeviceGuid() == toppingClsid, "the device GUID is the target CLSID");
		harness.expect(out->getDeviceString() == std::wstring(L"ASIO Topping USB Audio Device ") + toppingClsid, "the Device: string is connection, name, CLSID");
		harness.expect(out->getTransportLabel() == L"ASIO", "the transport label is the one word");
		harness.expectFalse(out->isDefaultDevice(), "never the default device");
		harness.expectFalse(out->isInstalled(), "not installed before install()");
		harness.expectFalse(in->isInstalled(), "the capture record is not installed either");
		harness.expectEqual(out->getChannelCount(), 0u, "no facts: channel count unknown");
		harness.expectEqual(out->getChannelMask(), 0ul, "no facts: mask unknown");

		out->install();
		harness.expectTrue(out->isInstalled(), "the playback record is installed");
		harness.expectFalse(in->isInstalled(), "the capture record is untouched");
		const std::wstring wrapper = AsioRegistration::wrapperClsidFor(toppingClsid);
		eapo::asio::WrapperRecord record;
		harness.require(eapo::asio::WrapperRecords::read(registry, wrapper, record), "the wrapper record exists");
		harness.expectTrue(record.options.processOutput, "ProcessOutput is on");
		harness.expectFalse(record.options.processInput, "ProcessInput stays off");
		harness.expect(record.targetClsid == toppingClsid, "the record names the target");
		harness.expect(registry.readValue(AsioRegistration::classesClsidRoot(false) + L"\\" + wrapper + L"\\InprocServer32", L"")
			== installDirectory().path() + L"\\EqualizerAPOAsio.dll", "the class tree points at the install directory's DLL");
		harness.expectFalse(registry.keyExists(AsioRegistration::classesClsidRoot(true) + L"\\" + wrapper), "no 32-bit view when the x86 DLL file is absent");

		// A second enumeration sees the state.
		std::vector<std::shared_ptr<AbstractAPOInfo>> again;
		AsioAPOInfo::appendInfos(again, false, registry);
		bool seenInstalled = false;
		for (const std::shared_ptr<AbstractAPOInfo>& info : again)
			seenInstalled = seenInstalled || (info->getDeviceName() == L"Topping USB Audio Device" && info->isInstalled());
		harness.expect(seenInstalled, "a fresh record reads the installed state back");

		in->install();
		harness.require(eapo::asio::WrapperRecords::read(registry, wrapper, record), "the wrapper record still exists");
		harness.expectTrue(record.options.processInput && record.options.processOutput, "both directions on after the capture install");

		out->uninstall();
		harness.expectFalse(out->isInstalled(), "playback uninstalled");
		harness.require(eapo::asio::WrapperRecords::read(registry, wrapper, record), "the record stays while capture is on");
		harness.expectTrue(record.options.processInput, "capture still on");
		harness.expectTrue(AsioRegistration::wrapperRegistered(registry, static_cast<AsioAPOInfo*>(out.get())->getTarget()), "the entry stays while one direction is on");

		in->uninstall();
		harness.expectFalse(eapo::asio::WrapperRecords::read(registry, wrapper, record), "the record goes with the last direction");
		harness.expectFalse(AsioRegistration::wrapperRegistered(registry, static_cast<AsioAPOInfo*>(out.get())->getTarget()), "the entry goes with the last direction");

		out->reinstall();
		harness.expectTrue(out->isInstalled(), "reinstall installs");

		// The synchronous mode is the record's one option, shared by both directions.
		AsioAPOInfo* asioOut = static_cast<AsioAPOInfo*>(out.get());
		harness.expectFalse(asioOut->isSynchronous(), "pipelined by default");
		harness.expectFalse(asioOut->hasChanges(), "no change before the option is touched");
		asioOut->setSynchronous(true);
		harness.expectTrue(asioOut->hasChanges(), "selecting the option is a change on an installed record");
		asioOut->reinstall();
		harness.require(eapo::asio::WrapperRecords::read(registry, wrapper, record), "the record survives the reinstall");
		harness.expect(record.options.mode == eapo::asio::Mode::Sync, "the record now runs synchronous");
		harness.expectFalse(asioOut->hasChanges(), "applied: no change pending");
		harness.expectTrue(asioOut->isSynchronous(), "the option reads back");
		out->uninstall();
	}

	void testBootAndHost32Options()
	{
		test::FakeRegistry registry;
		seedTargets(registry);
		std::vector<std::shared_ptr<AbstractAPOInfo>> playback;
		AsioAPOInfo::appendInfos(playback, false, registry);
		AsioAPOInfo* topping = nullptr;
		AsioAPOInfo* minidsp = nullptr;
		for (const std::shared_ptr<AbstractAPOInfo>& info : playback)
		{
			AsioAPOInfo* record = static_cast<AsioAPOInfo*>(info.get());
			if (record->getDeviceGuid() == toppingClsid)
				topping = record;
			else if (record->getDeviceGuid() == minidspClsid)
				minidsp = record;
		}
		harness.require(topping != nullptr && minidsp != nullptr, "both playback records exist");
		harness.expectFalse(topping->isAutoStart(), "start at boot is off by default");
		harness.expectFalse(topping->isHost32(), "32-bit host support is off by default");
		harness.expectEqual(topping->getDeadlinePercent(), 25u, "the wait defaults to a quarter of the buffer");
		harness.expectFalse(topping->canHost32(), "no x86 wrapper file beside this test: 32-bit support unavailable");

		topping->install();
		harness.expectFalse(AsioRegistration::autoStartRegistered(registry), "installing without the option writes no Run value");
		topping->setAutoStart(true);
		harness.expectTrue(topping->hasChanges(), "turning the option on is a change on an installed record");
		topping->reinstall();
		harness.expectTrue(AsioRegistration::autoStartRegistered(registry), "the Run value appears");
		harness.expect(registry.readValue(AsioRegistration::autoStartKey(), AsioRegistration::autoStartValueName())
			== L"\"" + installDirectory().path() + L"\\EqualizerAPOHost.exe\" --resident", "the Run value starts the host resident");
		harness.expect(topping->isAutoStart() && !topping->hasChanges(), "applied: the option reads back, no change pending");

		minidsp->setAutoStart(true);
		minidsp->install();
		topping->setAutoStart(false);
		topping->reinstall();
		harness.expectTrue(AsioRegistration::autoStartRegistered(registry), "the value stays while another target asks for it");
		minidsp->uninstall();
		harness.expectFalse(AsioRegistration::autoStartRegistered(registry), "the last target's uninstall removes it");

		topping->setDeadlinePercent(75);
		topping->setHost32(true);
		harness.expectTrue(topping->hasChanges(), "the wait share and 32-bit support are changes too");
		topping->reinstall();
		eapo::asio::WrapperRecord record;
		harness.require(eapo::asio::WrapperRecords::read(registry, topping->getWrapperClsid(), record), "the record exists");
		harness.expectEqual(record.options.deadlinePercent, 75u, "the wait share is in the record");
		harness.expectTrue(record.register32, "32-bit support is in the record");
		harness.expectFalse(registry.keyExists(AsioRegistration::classesClsidRoot(true) + L"\\" + topping->getWrapperClsid()),
			"without the x86 file the 32-bit view still stays empty");
		harness.expectEqual(topping->getDeadlinePercent(), 75u, "the wait share reads back");
		harness.expectFalse(topping->hasChanges(), "applied");

		// With the x86 wrapper in the install directory the same record
		// registers the 32-bit view.
		const std::wstring x86 = installDirectory().path() + L"\\x86";
		CreateDirectoryW(x86.c_str(), nullptr);
		const std::wstring wrapper32 = AsioRegistration::wrapper32DllPath(installDirectory().path());
		CloseHandle(CreateFileW(wrapper32.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
		harness.expectTrue(topping->canHost32(), "an x86 wrapper in the install directory makes 32-bit support available");
		topping->reinstall();
		harness.expect(registry.keyExists(AsioRegistration::classesClsidRoot(true) + L"\\" + topping->getWrapperClsid())
			&& registry.readValue(AsioRegistration::classesClsidRoot(true) + L"\\" + topping->getWrapperClsid() + L"\\InprocServer32", L"")
				== wrapper32, "and the 32-bit class tree points at it");
		DeleteFileW(wrapper32.c_str());
		RemoveDirectoryW(x86.c_str());
		topping->uninstall();
		harness.expectFalse(eapo::asio::WrapperRecords::read(registry, topping->getWrapperClsid(), record), "the record goes with the last direction");
	}

	void testFactsFeedTheRecord()
	{
		test::FakeRegistry registry;
		seedTargets(registry);
		// What the engine host writes (EngineHostCore publishes through the
		// same function), read back by the records.
		eapo::asio::StreamFormat format;
		format.sampleRate = 96000.0;
		format.frames = 128;
		format.channels[0] = 2;
		format.channels[1] = 0;
		wcsncpy_s(format.deviceName, L"Topping USB Audio Device", _TRUNCATE);
		wcsncpy_s(format.deviceGuid, toppingClsid, _TRUNCATE);
		eapo::asio::StreamFacts::write(registry, format);
		const std::wstring facts = eapo::asio::StreamFacts::key(toppingClsid);
		harness.expect(facts == std::wstring(USER_REGPATH) + L"\\ASIO\\" + toppingClsid, "the facts live under the user's hive, keyed by the target CLSID");
		harness.expectEqual(registry.readDWORDValue(facts, L"Frames"), 128ul, "the buffer size is kept for diagnostics");
		harness.expect(registry.readValue(facts, L"DeviceName") == L"Topping USB Audio Device", "and so is the driver's name");
		std::vector<std::shared_ptr<AbstractAPOInfo>> playback, capture;
		AsioAPOInfo::appendInfos(playback, false, registry);
		AsioAPOInfo::appendInfos(capture, true, registry);
		std::shared_ptr<AbstractAPOInfo> out, in;
		for (const std::shared_ptr<AbstractAPOInfo>& info : playback)
		{
			if (info->getDeviceGuid() == toppingClsid)
				out = info;
		}
		for (const std::shared_ptr<AbstractAPOInfo>& info : capture)
		{
			if (info->getDeviceGuid() == toppingClsid)
				in = info;
		}
		harness.require(out != nullptr && in != nullptr, "records found");
		harness.expectEqual(out->getSampleRate(), 96000u, "the sample rate comes from the facts");
		harness.expectEqual(out->getChannelCount(), 2u, "the output channel count comes from the facts");
		harness.expectEqual(out->getChannelMask(), 3ul, "two channels derive the stereo mask");
		harness.expectEqual(in->getChannelCount(), 0u, "the capture record reads its own count");

		eapo::asio::StreamShape shape;
		harness.expectFalse(eapo::asio::StreamFacts::read(registry, minidspClsid, shape), "a target never streamed has no facts");
		harness.expectEqual(shape.sampleRate, 0ul, "and reads as zero");
	}

	// Audit #348 TD-45: a target CLSID that is not a GUID derived an empty
	// wrapper CLSID, and registering it wrote the class values into the
	// CLSID root itself.
	void testNonGuidTargetIsRefused()
	{
		test::FakeRegistry registry;
		seedTargets(registry);
		const std::wstring root = AsioRegistration::asioRoot(false);
		registry.seedKey(root + L"\\ProgID Driver");
		registry.seedString(root + L"\\ProgID Driver", L"CLSID", L"Vendor.AsioDriver");
		bool listed = false;
		for (const AsioTarget& target : AsioRegistration::enumerateTargets(registry))
			listed = listed || target.name == L"ProgID Driver";
		harness.expectFalse(listed, "a driver whose CLSID is not a GUID is not offered");

		AsioTarget target;
		target.name = L"ProgID Driver";
		target.clsid = L"Vendor.AsioDriver";
		target.description = target.name;
		bool refused = false;
		try
		{
			AsioRegistration::registerWrapper(registry, target, L"C:\\eapo\\EqualizerAPOAsio.dll", L"");
		}
		catch (const WideError&)
		{
			refused = true;
		}
		harness.expectTrue(refused, "registering it throws");
		harness.expectFalse(registry.keyExists(root + L"\\ProgID Driver (EQ APO XT)"), "and writes no entry");
		harness.expectFalse(registry.keyExists(AsioRegistration::classesClsidRoot(false)), "nor anything under the CLSID root");

		AsioRegistration::unregisterWrapper(registry, target);
		harness.expect(registry.keyExists(root + L"\\ProgID Driver"), "unregistering it touches nothing, the driver's own entry included");
	}

	AsioAPOInfo* findPlaybackRecord(const std::vector<std::shared_ptr<AbstractAPOInfo>>& list, const wchar_t* clsid)
	{
		for (const std::shared_ptr<AbstractAPOInfo>& info : list)
		{
			AsioAPOInfo* record = static_cast<AsioAPOInfo*>(info.get());
			if (record->getDeviceGuid() == clsid)
				return record;
		}
		return nullptr;
	}

	// Audit #348 C2/TD-31: the ASIO adapter wrote its record, the entry in
	// both views and the Run value straight to the registry, so a failure
	// midway left a record with no entry, and nothing was logged or
	// reported. The three operations now run in one transaction with a
	// report, like the endpoint adapter's.
	void testOperationsAreTransactionalAndReported()
	{
		const std::wstring entryKey = AsioRegistration::asioRoot(false) + L"\\"
			+ AsioRegistration::entryNameFor(L"Topping USB Audio Device");
		const std::wstring recordKey = eapo::asio::WrapperRecords::recordKey(AsioRegistration::wrapperClsidFor(toppingClsid));

		{
			test::FakeRegistry registry;
			seedTargets(registry);
			std::vector<std::shared_ptr<AbstractAPOInfo>> playback;
			AsioAPOInfo::appendInfos(playback, false, registry);
			AsioAPOInfo* topping = findPlaybackRecord(playback, toppingClsid);
			harness.require(topping != nullptr, "the playback record exists");

			// The entry's Description is written after the record: failing it
			// stops the install midway.
			registry.failValueWrite(entryKey, L"Description");
			bool threw = false;
			try
			{
				topping->install();
			}
			catch (const WideError& e)
			{
				threw = true;
				harness.expectFalse(e.getMessage().empty(), "the failure carries a message");
			}
			harness.expectTrue(threw, "a failing install throws a WideError");
			harness.expectFalse(registry.keyExists(recordKey), "the record written before the failure is taken back");
			harness.expectFalse(registry.keyExists(entryKey), "no half-written entry is left");
			harness.expectFalse(topping->isInstalled(), "the record reads as not installed");
			const DeviceInstallReport& failed = topping->getLastOperationReport();
			harness.expect(failed.operation == DeviceInstallReport::Operation::Install, "the report names the install");
			harness.expect(failed.outcome == DeviceInstallReport::Outcome::Failed, "and says it failed");
			harness.expectFalse(failed.failure.empty(), "with the reason");
			harness.expectFalse(failed.leftInconsistent(), "and a complete rollback");
		}

		{
			test::FakeRegistry registry;
			seedTargets(registry);
			std::vector<std::shared_ptr<AbstractAPOInfo>> playback;
			AsioAPOInfo::appendInfos(playback, false, registry);
			AsioAPOInfo* topping = findPlaybackRecord(playback, toppingClsid);
			harness.require(topping != nullptr, "the playback record exists again");

			topping->install();
			const DeviceInstallReport& done = topping->getLastOperationReport();
			harness.expect(done.outcome == DeviceInstallReport::Outcome::Succeeded, "a clean install reports success");
			harness.expect(done.asioEntry == AsioRegistration::entryNameFor(L"Topping USB Audio Device"), "and names the entry");
			harness.expectFalse(done.appliedOperations.empty(), "and lists what it wrote");

			// A reinstall that fails in its install half must not leave the
			// entry removed by its uninstall half.
			topping->setSynchronous(true);
			registry.failValueWrite(recordKey, L"Mode");
			bool threw = false;
			try
			{
				topping->reinstall();
			}
			catch (const WideError&)
			{
				threw = true;
			}
			harness.expectTrue(threw, "the failing reinstall throws");
			harness.expectTrue(registry.keyExists(entryKey), "the entry is still registered");
			harness.expectTrue(registry.keyExists(recordKey), "and its record is back");
			harness.expectTrue(topping->isInstalled(), "the record still reads as installed");
			harness.expect(topping->getLastOperationReport().operation == DeviceInstallReport::Operation::Reinstall,
				"the report names the reinstall");
		}

		{
			// Without the InstallPath value there is no wrapper DLL to point
			// at. The endpoint adapter says so with a DeviceException, and so
			// does this one now (it used to surface readValue's RegistryError).
			test::FakeRegistry registry;
			seedTargets(registry);
			registry.deleteValue(APP_REGPATH, L"InstallPath");
			std::vector<std::shared_ptr<AbstractAPOInfo>> playback;
			AsioAPOInfo::appendInfos(playback, false, registry);
			AsioAPOInfo* topping = findPlaybackRecord(playback, toppingClsid);
			harness.require(topping != nullptr, "the playback record exists without InstallPath");
			harness.expectFalse(topping->canHost32(), "32-bit support reads as unavailable instead of throwing");
			bool deviceError = false;
			try
			{
				topping->install();
			}
			catch (const DeviceException&)
			{
				deviceError = true;
			}
			harness.expectTrue(deviceError, "a missing InstallPath is a DeviceException");
			harness.expectFalse(registry.keyExists(recordKey), "and nothing is written");
			harness.expectFalse(topping->changesNeedAudioRestart(), "an ASIO entry needs no audio service restart");
		}
	}
}

int runDeviceRecordTests()
{
	testEnumerationAndDerivedIds();
	testRegisterAndUnregisterBothViews();
	testWasapiRecordAndEndpointTarget();
	testUnregisterFindsAnEntryUnderItsOldName();
	testRecordsShareOneWrapperRecord();
	testBootAndHost32Options();
	testFactsFeedTheRecord();
	testNonGuidTargetIsRefused();
	testOperationsAreTransactionalAndReported();
	installDirectory().removeAll();
	harness.report();
	return 0;
}
