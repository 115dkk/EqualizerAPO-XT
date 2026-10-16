/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTBusVerdict.h"

namespace
{
QString name(VST3BusLayout layout)
{
	return QString::fromWCharArray(vst3BusLayoutName(layout));
}

VSTBusFacts loadedVst3(VST3BusLayout input, VST3BusLayout output)
{
	VSTBusFacts facts;
	facts.hasContract = input != VST3BusLayout::Auto || output != VST3BusLayout::Auto;
	facts.requestedInput = input;
	facts.requestedOutput = output;
	facts.instanceLoaded = true;
	facts.isVST3 = true;
	return facts;
}
}

// Audit #422 D4: the modern VST card's bus table, row by row. The VST2 and
// stale-keys rows were field bugs (issue #216).
void testVSTBusVerdictCoversEveryRow()
{
	using Severity = ReferenceCardState::Severity;
	using Tone = VSTBusVerdict::Tone;
	using Readout = VSTBusVerdict::Readout;

	{
		const VSTBusVerdict verdict = VSTBusVerdict::describe(VSTBusFacts());
		expectFalse(verdict.stripVisible, "no plug-in and no contract: nothing to show");
		expectTrue(verdict.statusText.isEmpty(), "a hidden strip says nothing");
	}

	{
		VSTBusFacts facts;
		facts.hasContract = true;
		facts.requestedInput = VST3BusLayout::Stereo;
		const VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectTrue(verdict.stripVisible, "a saved contract stays visible while the library is missing");
		expectFalse(verdict.selectorsEnabled, "the selectors wait for the plug-in");
		expectEqual(verdict.selectorsDisabledReason,
			QStringLiteral("The bus layout can be changed after the plugin loads."), "locked before the load");
		expectTrue(verdict.readout == Readout::Single && verdict.verdictText.isEmpty()
			&& verdict.tone == Tone::Neutral, "no verdict before the load");
		expectTrue(verdict.statusText.isEmpty(), "nothing on the status line before the load");
	}

	{
		VSTBusFacts facts;
		facts.instanceLoaded = true;
		facts.isVST3 = false;
		VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectTrue(verdict.stripVisible, "a loaded plug-in shows the strip");
		expectFalse(verdict.selectorsEnabled, "VST2 locks the selectors");
		expectEqual(verdict.selectorsDisabledReason,
			QStringLiteral("Input and Output layouts are only supported for VST3 plugins."), "VST2 lock reason");
		expectTrue(verdict.readout == Readout::Single && verdict.verdictText == QStringLiteral("VST2")
			&& verdict.tone == Tone::Neutral, "a bare VST2 line reads VST2, neutral");
		expectTrue(verdict.statusText.isEmpty(), "a bare VST2 line has nothing to repair");

		facts.hasContract = true;
		facts.requestedInput = VST3BusLayout::Stereo;
		verdict = VSTBusVerdict::describe(facts);
		expectTrue(verdict.tone == Tone::Warning, "stale keys on a VST2 module warn");
		expectEqual(verdict.statusText,
			QStringLiteral("This module loaded as VST2 and ignores the saved Input/Output layouts. Remove them via Options."),
			"stale keys point at the repair");
		expectTrue(verdict.statusSeverity == Severity::Warning, "stale keys are a warning");
	}

	{
		VSTBusFacts facts = loadedVst3(VST3BusLayout::Stereo, VST3BusLayout::Auto);
		facts.embedded = true;
		facts.migratedLegacyStereoInput = true;
		const VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectFalse(verdict.selectorsEnabled, "an embedded panel locks the selectors");
		expectEqual(verdict.selectorsDisabledReason,
			QStringLiteral("Close the plugin panel to change the bus layout."), "embedded lock reason");
		expectTrue(verdict.readout == Readout::Keep, "the last verdict stays while the panel is embedded");
		expectTrue(verdict.statusText.isEmpty(), "an embedded panel adds nothing to the status line");
		expectFalse(facts.negotiates(), "no negotiation under an embedded panel");
	}

	{
		VSTBusFacts facts = loadedVst3(VST3BusLayout::Stereo, VST3BusLayout::Surround51);
		expectTrue(facts.negotiates(), "a loaded VST3 plug-in without a panel negotiates");
		facts.accepted = false;
		const VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectTrue(verdict.selectorsEnabled, "a rejected contract can be changed right there");
		expectTrue(verdict.selectorsDisabledReason.isEmpty(), "enabled selectors carry no reason");
		expectTrue(verdict.readout == Readout::Single && verdict.verdictText.isEmpty()
			&& verdict.tone == Tone::Critical, "a rejection is a lamp, not a word");
		expectEqual(verdict.statusText,
			QStringLiteral("The plugin rejected %1 in / %2 out. Audio passes through unchanged.")
				.arg(name(VST3BusLayout::Stereo), name(VST3BusLayout::Surround51)),
			"the status line names the rejected pair");
		expectTrue(verdict.statusSeverity == Severity::Critical, "a rejection is critical");
	}

	{
		VSTBusFacts facts = loadedVst3(VST3BusLayout::Stereo, VST3BusLayout::Surround51);
		facts.accepted = true;
		const VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectFalse(facts.anyAuto(), "an all-explicit contract has no Auto side");
		expectTrue(verdict.readout == Readout::Single && verdict.verdictText.isEmpty()
			&& verdict.tone == Tone::Success, "an accepted explicit contract is a lamp");
		expectTrue(verdict.statusText.isEmpty(), "an accepted explicit contract needs no words");
	}

	{
		VSTBusFacts facts = loadedVst3(VST3BusLayout::Stereo, VST3BusLayout::Auto);
		facts.accepted = true;
		facts.negotiatedInput = VST3BusLayout::Stereo;
		facts.negotiatedOutput = VST3BusLayout::Surround51;
		VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectTrue(verdict.readout == Readout::Pair, "an Auto side prints what engaged");
		expectEqual(verdict.verdictInput, name(VST3BusLayout::Stereo), "the negotiated input");
		expectEqual(verdict.verdictOutput, name(VST3BusLayout::Surround51), "the negotiated output");
		expectTrue(verdict.tone == Tone::Success, "an accepted contract with an Auto side succeeds");

		facts.negotiatedInput.reset();
		facts.negotiatedOutput.reset();
		facts.negotiatedInputChannels = 2;
		facts.negotiatedOutputChannels = 7;
		verdict = VSTBusVerdict::describe(facts);
		expectEqual(verdict.verdictInput, QStringLiteral("2 ch"), "an arrangement outside the grammar reads as a count");
		expectEqual(verdict.verdictOutput, QStringLiteral("7 ch"), "the output count likewise");

		facts.migratedLegacyStereoInput = true;
		verdict = VSTBusVerdict::describe(facts);
		expectEqual(verdict.statusText,
			QStringLiteral("The legacy Stereo input option now reads as Input Stereo, Output Auto and will be saved that way."),
			"a migrated StereoInput flag is explained");
		expectTrue(verdict.statusSeverity == Severity::None, "the migration note carries no severity");
	}

	{
		VSTBusFacts facts = loadedVst3(VST3BusLayout::Auto, VST3BusLayout::Auto);
		facts.accepted = true;
		facts.negotiatedInput = VST3BusLayout::Stereo;
		facts.negotiatedOutput = VST3BusLayout::Stereo;
		const VSTBusVerdict verdict = VSTBusVerdict::describe(facts);
		expectTrue(verdict.readout == Readout::Pair && verdict.tone == Tone::Neutral,
			"a bare Auto line prints what engaged, neutral");
		expectTrue(verdict.statusText.isEmpty(), "a bare Auto line has nothing to say");
	}
}
