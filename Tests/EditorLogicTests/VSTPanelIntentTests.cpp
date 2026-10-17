/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTPanelIntent.h"

// Audit #422 D3: what the user asked of the panel while the library loaded,
// and what each VST row does once the load lands. Both rows used to carry a
// copy of this; VSTPluginSession now drives the one machine.
TEST_CASE(EditorLogicTests, testVSTPanelIntentCompletesWishesOnLoad)
{
	{
		VSTPanelIntent intent;
		const VSTPanelIntent::Completion done = intent.loadFinished(true);
		expectFalse(done.storeLine || done.embed || done.dropEmbed || done.openDialogUnlessEmbedded,
			"a load nobody waited for asks for nothing");
	}

	{
		VSTPanelIntent intent;
		intent.parkEmbed();
		expectTrue(intent.embedPending(), "an embed asked for while loading waits");
		const VSTPanelIntent::Completion done = intent.loadFinished(true);
		expectTrue(done.embed, "a loaded plug-in embeds the panel the user asked for");
		expectFalse(done.dropEmbed, "a loaded plug-in keeps the embed option");
		expectFalse(intent.embedPending(), "the load consumes the waiting embed");
		expectFalse(intent.loadFinished(true).embed, "a second load does not embed again");
	}

	{
		VSTPanelIntent intent;
		intent.parkEmbed();
		const VSTPanelIntent::Completion done = intent.loadFinished(false);
		expectFalse(done.embed, "a failed load does not embed");
		expectTrue(done.dropEmbed, "a failed load drops the embed option instead of retrying");
	}

	{
		VSTPanelIntent intent;
		intent.parkEmbed();
		intent.dropEmbed();
		expectFalse(intent.embedPending(), "switching the option off drops the waiting embed");
		const VSTPanelIntent::Completion done = intent.loadFinished(true);
		expectFalse(done.embed || done.dropEmbed, "a dropped embed leaves the load alone");
	}

	{
		VSTPanelIntent intent;
		intent.parkDialog();
		expectTrue(intent.dialogPending(), "a panel asked for while loading waits");
		expectTrue(intent.loadFinished(true).openDialogUnlessEmbedded,
			"a loaded plug-in opens the dialog the user asked for");
		expectFalse(intent.dialogPending(), "the load consumes the waiting dialog");

		intent.parkDialog();
		expectFalse(intent.loadFinished(false).openDialogUnlessEmbedded,
			"a failed load opens no dialog");
		expectFalse(intent.dialogPending(), "a failed load consumes the waiting dialog too");
	}

	{
		VSTPanelIntent intent;
		intent.replacementStarted();
		expectTrue(intent.replacementPending(), "a replaced library waits to store its line");
		expectTrue(intent.loadFinished(false).storeLine,
			"the line is stored once the replaced library's load ends, failed or not");
		expectFalse(intent.loadFinished(true).storeLine, "the line is stored once per replacement");
	}

	{
		VSTPanelIntent intent;
		intent.replacementStarted();
		intent.parkEmbed();
		intent.parkDialog();
		const VSTPanelIntent::Completion done = intent.loadFinished(true);
		expectTrue(done.storeLine && done.embed && done.openDialogUnlessEmbedded,
			"every waiting wish completes on the same load");
		expectFalse(intent.embedPending() || intent.dialogPending() || intent.replacementPending(),
			"nothing waits after the load");
	}
}
