/*
	This file is part of EqualizerAPO-XT.

	Card selection models: Channel, Device and Stage. Each model must write
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
	// semantics, ChannelHelper::getChannelIndex), and are written back
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
