/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Card selection models: Channel, Device and Stage, and the NewChannel
	card's name list and the Send card's state. Each model must write
	the same bytes the legacy dialogs produced for an equivalent selection,
	so every check here is a serialization identity.
*/

#include <string>
#include <vector>

#include <QList>
#include <QString>
#include <QStringList>

#include "Editor/widgets/cards/ChannelSelectionModel.h"
#include "Editor/widgets/cards/DeviceSelectionModel.h"
#include "Editor/widgets/cards/NewChannelListModel.h"
#include "Editor/widgets/cards/SendCardModel.h"
#include "Editor/widgets/cards/StageSelectionModel.h"

#include "EditorLogicTestSupport.h"

void testChannelSelectionModel()
{
	// ChannelSelectionModel serialization identity: for equivalent
	// selections the in-place chip editor must write the same bytes the
	// legacy multi-select dialog produced (standard positions in the
	// dialog's checkbox order, then non-standard device channels, then
	// custom names).
	const std::vector<std::wstring> stereo = { L"L", L"R" };
	const std::vector<std::wstring> surround51 = { L"L", L"R", L"C", L"LFE", L"RL", L"RR" };
	const std::vector<std::wstring> surround71 = { L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" };

	ChannelSelectionModel model;
	model.load("L R C", surround51);
	expectEqual(model.serialize(), "C L R", "5.1 selection must serialize in dialog order");

	model.load("R L", stereo);
	expectEqual(model.serialize(), "L R", "written order canonicalizes like the dialog");

	// Position numbers resolve against the device order (engine
	// semantics, ChannelLayout::getChannelIndex), and are written back
	// as names like the dialog did.
	model.load("2", stereo);
	expectEqual(model.serialize(), "R", "numeric selector resolves in device order");

	// Historical aliases follow the engine: SUB -> LFE, SL <-> RL.
	model.load("SUB", surround51);
	expectEqual(model.serialize(), "LFE", "SUB alias selects the LFE chip");
	model.load("SL", surround51);
	expectEqual(model.serialize(), "RL", "SL on a back-channel device selects RL");

	model.load("SR SL LFE", surround71);
	expectEqual(model.serialize(), "SL SR LFE", "7.1 selection serializes in dialog order");

	// ALL wins over individual selections, exactly like the dialog.
	model.load("ALL L", surround51);
	expectTrue(model.allSelected(), "ALL token sets the all-channels state");
	expectEqual(model.serialize(), "ALL", "ALL serializes alone");

	// Picking a seat while ALL is on narrows to that seat: ALL releases and
	// the stale individual selection (the L above) clears with it. The
	// legacy dialog, and the chip row until v2.46, made the user uncheck ALL
	// before any seat would take a click.
	model.toggle("R");
	expectFalse(model.allSelected(), "a chip pick releases ALL");
	expectEqual(model.serialize(), "R", "the pick is the whole selection after ALL");
	model.toggle("C");
	expectEqual(model.serialize(), "C R", "later picks accumulate as before");
	model.load("ALL", stereo);
	expectTrue(model.addCustom("vsl"), "a custom name is accepted while ALL is on");
	expectFalse(model.allSelected(), "a custom name releases ALL too");
	expectEqual(model.serialize(), "VSL", "the custom name is the whole selection after ALL");

	// Custom/virtual channels keep their written order after the device
	// chips, matching the dialog's list section.
	model.load("VSL L VSR", stereo);
	expectEqual(model.serialize(), "L VSL VSR", "custom names follow device channels");
	model.toggle("R");
	expectEqual(model.serialize(), "L R VSL VSR", "toggling keeps canonical order");
	model.toggle("L");
	expectEqual(model.serialize(), "R VSL VSR", "deselecting removes the token");

	expectFalse(model.addCustom("  "), "blank custom name is rejected");
	expectFalse(model.addCustom("A B"), "multi-token custom name is rejected");
	expectTrue(model.addCustom(" vrr "), "custom name is trimmed and accepted");
	expectEqual(model.serialize(), "R VSL VSR VRR", "added custom name serializes upper-cased");

	// addCustom resolves aliases against the device set too: SUB selects
	// the LFE chip instead of duplicating it as a custom name.
	model.load("", surround51);
	expectTrue(model.addCustom("sub"), "SUB through addCustom is accepted");
	expectEqual(model.serialize(), "LFE", "SUB resolves to the device's LFE chip");
}

void testDeviceSelectionModel()
{
	// DeviceSelectionModel serialization identity: for equivalent device
	// selections the in-place chip editor must write the same bytes the
	// legacy change-button dialog produced - "all", or each selected
	// device's device string joined with "; " in list order (output
	// devices first, then input). Matching runs through the shared
	// DeviceCommand codec, the same one the engine uses, so a chip is
	// pre-selected exactly when the engine would match that device.
	auto dev = [](const QString& deviceString, const QString& name, bool installed, bool isInput) {
		DeviceEntry e;
		e.deviceString = deviceString;
		e.name = name;
		e.installed = installed;
		e.isInput = isInput;
		return e;
	};
	const QString devSpeakers = "Speakers Realtek HD Audio {0.0.0.00000000}.{aaaaaaaa-1111-2222-3333-444444444444}";
	const QString devHeadphones = "Headphones Realtek HD Audio {0.0.0.00000000}.{bbbbbbbb-1111-2222-3333-444444444444}";
	const QString devDigital = "Digital Output Realtek HD Audio {0.0.0.00000000}.{cccccccc-1111-2222-3333-444444444444}";
	const QString devMic = "Microphone Realtek HD Audio {0.0.1.00000000}.{dddddddd-1111-2222-3333-444444444444}";
	const QList<DeviceEntry> devices = {
		dev(devSpeakers, "Speakers", true, false),
		dev(devHeadphones, "Headphones", true, false),
		dev(devDigital, "Digital Output", false, false),
		dev(devMic, "Microphone", true, true),
	};

	DeviceSelectionModel model;

	// The literal lowercase "all" line is the all-devices state, like the
	// dialog's "All devices" choice: it round-trips to "all" and marks no
	// individual chip selected.
	model.load("all", devices);
	expectTrue(model.allSelected(), "literal 'all' sets the all-devices state");
	expectEqual(model.serialize(), "all", "all-devices serializes back as 'all'");

	// An empty parameter is not the all state and selects nothing.
	model.load("", devices);
	expectFalse(model.allSelected(), "empty parameter is not the all state");
	expectEqual(model.serialize(), "", "no selection serializes empty");

	// A full device-string pattern pre-selects exactly that endpoint and
	// round-trips byte-for-byte, GUID included.
	model.load(devSpeakers, devices);
	expectFalse(model.allSelected(), "a specific device is not the all state");
	expectEqual(model.serialize(), devSpeakers, "single device round-trips verbatim");

	// A bare word matches as a case-insensitive substring (DeviceCommand
	// semantics) and is rewritten to the matched device's full string.
	model.load("headphones", devices);
	expectEqual(model.serialize(), devHeadphones, "word pattern selects and canonicalizes to the device string");

	// Multiple patterns, written input-first, serialize in list order
	// (output devices first, then input) joined with "; ".
	model.load(devMic + "; " + devSpeakers, devices);
	expectEqual(model.serialize(), devSpeakers + "; " + devMic, "multiple devices serialize in list order");

	// toggle() flips one chip and keeps canonical list order.
	model.load(devSpeakers, devices);
	model.toggle(devHeadphones);
	expectEqual(model.serialize(), devSpeakers + "; " + devHeadphones, "toggling on adds a chip in list order");
	model.toggle(devSpeakers);
	expectEqual(model.serialize(), devHeadphones, "toggling off removes the token");

	// "All devices" wins over individual selections, like the dialog.
	model.load(devSpeakers, devices);
	model.setAllSelected(true);
	expectEqual(model.serialize(), "all", "All overrides individual selections");

	// Picking a device while "all" is on narrows to that device: all
	// releases and the stale individual selection (Speakers) clears.
	model.toggle(devHeadphones);
	expectFalse(model.allSelected(), "a chip pick releases all");
	expectEqual(model.serialize(), devHeadphones, "the pick is the whole selection after all");
	model.toggle(devSpeakers);
	expectEqual(model.serialize(), devSpeakers + "; " + devHeadphones, "later picks accumulate as before");
}

void testStageSelectionModel()
{
	// StageSelectionModel serialization identity: known stages come back in
	// the legacy checkbox GUI's canonical order (pre-mix, post-mix,
	// capture), case-insensitively parsed through the shared StageCommand
	// codec; unlike the legacy GUI, tokens outside the vocabulary survive
	// an edit in their written order.
	StageSelectionModel model;

	model.load("post-mix pre-mix");
	expectTrue(model.isSelected("pre-mix") && model.isSelected("post-mix"), "both written stages are selected");
	expectFalse(model.isSelected("capture"), "capture stays unselected");
	expectEqual(model.serialize(), "pre-mix post-mix", "known stages serialize in canonical order");

	model.load("Pre-Mix CAPTURE");
	expectEqual(model.serialize(), "pre-mix capture", "selectors are case-insensitive and lower-cased");

	model.load("pre-mix render foo");
	expectEqual(model.unknownTokens().join(' '), "render foo", "unknown tokens are reported");
	expectEqual(model.serialize(), "pre-mix render foo", "unknown tokens survive after the known stages");
	model.setSelected("pre-mix", false);
	model.setSelected("capture", true);
	expectEqual(model.serialize(), "capture render foo", "toggles keep the unknown tokens");

	model.load("");
	expectEqual(model.serialize(), "", "an empty selection serializes empty (matches no stage)");
	model.setSelected("capture", true);
	expectEqual(model.serialize(), "capture", "a single selection writes just its token");
}

void testNewChannelListModel()
{
	// The NewChannel card's list: names are parsed and upper-cased by the
	// engine's own codec, each carries the engine's verdict against the
	// device, and an addition is all or nothing.
	NewChannelListModel model;
	model.setDeviceChannels({L"L", L"R", L"C", L"LFE", L"RL", L"RR"});

	model.load("vc, VRL  vc");
	expectEqual(model.names().join(' '), "VC VRL", "names are upper-cased and kept once");
	expectEqual(model.serialize(), "VC VRL", "the line is written with single spaces");
	expectFalse(model.hasProblems(), "virtual names have no problem");

	NewChannelListModel::AddOutcome added = model.add("vx, VC");
	expectEqual(added.added.join(' '), "VX", "an already listed name is skipped quietly");
	expectTrue(added.rejected.isEmpty(), "nothing was refused");
	expectEqual(model.serialize(), "VC VRL VX", "added names follow the existing ones");

	NewChannelListModel::AddOutcome refused = model.add("VY SL");
	expectEqual(refused.rejected, "SL", "an alias of a device channel is refused");
	expectTrue(refused.problem == NewChannelCommand::Problem::DeviceChannel, "the refusal says why");
	expectTrue(refused.added.isEmpty(), "one refused name adds none of them");
	expectEqual(model.serialize(), "VC VRL VX", "a refused addition leaves the list alone");

	expectTrue(model.add("2X").problem == NewChannelCommand::Problem::StartsWithDigit, "a leading digit is refused");
	expectTrue(model.add("All").problem == NewChannelCommand::Problem::ReservedAll, "ALL is refused in any case");
	expectTrue(model.add("A=B").problem == NewChannelCommand::Problem::SeparatorCharacter, "a Copy separator is refused");
	expectFalse(NewChannelListModel::problemText(NewChannelCommand::Problem::DeviceChannel).isEmpty(),
		"every problem has card wording");
	expectTrue(NewChannelListModel::problemText(NewChannelCommand::Problem::None).isEmpty(),
		"no problem, no wording");

	// A name the engine refuses stays as written, flagged, until removed.
	model.load("VC L");
	expectEqual(model.serialize(), "VC L", "a refused name in the file is kept");
	expectTrue(model.problem("L") == NewChannelCommand::Problem::DeviceChannel, "and flagged");
	expectTrue(model.hasProblems(), "the list reports the problem");
	model.remove("L");
	expectFalse(model.hasProblems(), "removing it clears the problem");
	expectEqual(model.serialize(), "VC", "the remaining name is written");
}

void testSendCardModel()
{
	// The Send card's state: targets exclude the device itself, a target the
	// machine lacks stays as written, the latency field reads back what it
	// shows, and the warning names the first thing that keeps the line from
	// working.
	const QString own = "{11111111-1111-1111-1111-111111111111}";
	const QString other = "{22222222-2222-2222-2222-222222222222}";
	std::vector<SendCardModel::Endpoint> endpoints(2);
	endpoints[0].guid = own;
	endpoints[0].name = "Playback 1/2";
	endpoints[0].channels = {L"L", L"R"};
	endpoints[1].guid = other.toUpper();
	endpoints[1].name = "Playback 3/4";
	endpoints[1].channels = {L"L", L"R"};
	endpoints[1].receives = true;

	SendCardModel model;
	expectTrue(model.load(""), "the picker's empty template loads");
	model.setEndpoints(own, endpoints);
	requireEqual(int(model.targets().size()), 1, "the device itself is not a target");
	expectEqual(model.targets()[0].guid, other, "targets carry the canonical GUID");
	expectEqual(model.targetIndex(), -1, "no target is written yet");
	expectFalse(model.warning().isEmpty(), "an empty line asks for a target");
	expectEqual(model.serialize(), "", "an empty card writes nothing after the colon");

	model.setTarget(other);
	expectEqual(model.targetIndex(), 0, "choosing a target selects it");
	expectFalse(model.warning().isEmpty(), "a line without a connection is called out");
	std::vector<Assignment> routed(2);
	routed[0].targetChannel = L"L";
	routed[0].sourceSum.push_back({1.0, false, L"SUB1"});
	routed[1].targetChannel = L"R";  // a seeded row with an empty sum
	model.setAssignments(routed);
	requireEqual(int(model.assignments().size()), 1, "rows with an empty sum are not written");
	expectTrue(model.warning().isEmpty(), "a connected line to a receiving endpoint is fine");
	expectEqual(model.serialize(), other + " L=SUB1", "the line names the target and the routing");

	expectTrue(model.setLatencyText("13.33"), "milliseconds without a unit");
	expectEqual(model.latencyText(), "13.33", "milliseconds read back as typed");
	expectTrue(model.setLatencyText("960 samples"), "whole samples with a unit");
	expectEqual(model.latencyText(), "960 samples", "samples read back with their unit");
	expectFalse(model.setLatencyText("1.5 samples"), "a fraction of a sample is refused");
	expectFalse(model.setLatencyText("-3"), "a negative latency is refused");
	expectEqual(model.latencyText(), "960 samples", "a refused entry leaves the latency alone");
	expectTrue(model.setLatencyText(""), "an empty field restores the default");
	expectEqual(model.latencyText(), "", "the default shows as an empty field");

	model.setMode(SendCommand::Mode::Replace);
	model.setCompensate(false);
	SendCardModel reread;
	expectTrue(reread.load(model.serialize()), "the written line loads again");
	expectTrue(reread.mode() == SendCommand::Mode::Replace, "the mode survives the round trip");
	expectFalse(reread.compensate(), "compensation off survives the round trip");

	// The receiver's Send option is off: the line works only while something
	// else plays there, and the card says so.
	endpoints[1].receives = false;
	model.setEndpoints(own, endpoints);
	expectFalse(model.warning().isEmpty(), "a target without the Send option is called out");

	// A target this machine does not have stays selectable as written.
	SendCardModel foreign;
	expectTrue(foreign.load("{33333333-3333-3333-3333-333333333333} L=L"), "a line to another machine's endpoint loads");
	foreign.setEndpoints(own, endpoints);
	expectEqual(foreign.targetIndex(), int(foreign.targets().size()) - 1, "the unknown target is listed last");
	expectFalse(foreign.targetKnown(), "and known to be unknown");
	expectFalse(foreign.warning().isEmpty(), "the card says the endpoint is missing");

	QString error;
	expectFalse(model.load("not-a-guid L=L", &error), "a malformed line does not load");
	expectFalse(error.isEmpty(), "and says why");
}
