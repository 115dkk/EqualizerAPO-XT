/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VSTBusVerdict.h"

#include <QCoreApplication>

namespace
{
QString layoutName(VST3BusLayout layout)
{
	return QString::fromWCharArray(vst3BusLayoutName(layout));
}
}

bool VSTBusFacts::negotiates() const
{
	return instanceLoaded && isVST3 && !embedded;
}

bool VSTBusFacts::anyAuto() const
{
	return requestedInput == VST3BusLayout::Auto || requestedOutput == VST3BusLayout::Auto;
}

// The texts keep the translation context of the card they were written for.
VSTBusVerdict VSTBusVerdict::describe(const VSTBusFacts& facts)
{
	VSTBusVerdict verdict;

	// No loaded plugin and no saved contract: nothing to show or protect. A
	// saved contract stays visible (though locked) even while the library is
	// missing, so reopening a config never hides data it still carries.
	verdict.stripVisible = facts.instanceLoaded || facts.hasContract;
	if (!verdict.stripVisible)
		return verdict;

	if (!facts.instanceLoaded)
	{
		verdict.selectorsDisabledReason = QCoreApplication::translate("VSTCardEditor", "The bus layout can be changed after the plugin loads.");
		verdict.readout = Readout::Single;
		return verdict;
	}

	if (!facts.isVST3)
	{
		// The loaded ABI decides: VST2 ignores the layout keys (issue #216).
		verdict.selectorsDisabledReason = QCoreApplication::translate("VSTCardEditor", "Input and Output layouts are only supported for VST3 plugins.");
		verdict.readout = Readout::Single;
		verdict.verdictText = QStringLiteral("VST2");
		if (facts.hasContract)
		{
			verdict.tone = Tone::Warning;
			verdict.statusText = QCoreApplication::translate("VSTCardEditor", "This module loaded as VST2 and ignores the saved Input/Output layouts. Remove them via Options.");
			verdict.statusSeverity = ReferenceCardState::Severity::Warning;
		}
		return verdict;
	}

	if (facts.embedded)
	{
		// Renegotiating would tear the processor state out from under the
		// open panel; the last verdict stays on display.
		verdict.selectorsDisabledReason = QCoreApplication::translate("VSTCardEditor", "Close the plugin panel to change the bus layout.");
		return verdict;
	}

	verdict.selectorsEnabled = true;

	if (!facts.accepted)
	{
		// Lamp-only, like the accepted verdict: a danger lamp beside the
		// selectors says it, and the sentence belongs to the status line. A
		// "Rejected" word in the strip restated the lamp (maintainer
		// judgement, r2). One short sentence: the fact and its consequence;
		// the escape routes are the selectors sitting right there (r3).
		verdict.readout = Readout::Single;
		verdict.tone = Tone::Critical;
		verdict.statusText = QCoreApplication::translate("VSTCardEditor", "The plugin rejected %1 in / %2 out. Audio passes through unchanged.")
			.arg(layoutName(facts.requestedInput), layoutName(facts.requestedOutput));
		verdict.statusSeverity = ReferenceCardState::Severity::Critical;
		return verdict;
	}

	// An accepted all-explicit contract needs no words - the selectors print
	// the pair, the lamp says it engaged. Any Auto direction makes the
	// negotiated result the informative fact, so the verdict prints what
	// actually engaged; an arrangement outside the config grammar honestly
	// reads as a channel count.
	if (facts.anyAuto())
	{
		verdict.readout = Readout::Pair;
		verdict.verdictInput = facts.negotiatedInput
			? layoutName(*facts.negotiatedInput) : QCoreApplication::translate("VSTCardEditor", "%1 ch").arg(facts.negotiatedInputChannels);
		verdict.verdictOutput = facts.negotiatedOutput
			? layoutName(*facts.negotiatedOutput) : QCoreApplication::translate("VSTCardEditor", "%1 ch").arg(facts.negotiatedOutputChannels);
		verdict.tone = facts.hasContract ? Tone::Success : Tone::Neutral;
	}
	else
	{
		verdict.readout = Readout::Single;
		verdict.tone = Tone::Success;
	}
	// Informational: it rides the status line without a severity.
	if (facts.migratedLegacyStereoInput)
		verdict.statusText = QCoreApplication::translate("VSTCardEditor", "The legacy Stereo input option now reads as Input Stereo, Output Auto and will be saved that way.");
	return verdict;
}
