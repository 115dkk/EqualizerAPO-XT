/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VSTPanelIntent.h"

void VSTPanelIntent::parkEmbed()
{
	embedWhenLoaded = true;
}

void VSTPanelIntent::dropEmbed()
{
	embedWhenLoaded = false;
}

void VSTPanelIntent::parkDialog()
{
	openWhenLoaded = true;
}

void VSTPanelIntent::replacementStarted()
{
	storeWhenLoaded = true;
}

bool VSTPanelIntent::embedPending() const
{
	return embedWhenLoaded;
}

bool VSTPanelIntent::dialogPending() const
{
	return openWhenLoaded;
}

bool VSTPanelIntent::replacementPending() const
{
	return storeWhenLoaded;
}

VSTPanelIntent::Completion VSTPanelIntent::loadFinished(bool instanceLoaded)
{
	Completion done;
	done.storeLine = storeWhenLoaded;
	storeWhenLoaded = false;

	// A failed load must not request another load through the panel actions:
	// the waiting embed is dropped instead of retried.
	if (embedWhenLoaded)
	{
		embedWhenLoaded = false;
		done.embed = instanceLoaded;
		done.dropEmbed = !instanceLoaded;
	}
	if (openWhenLoaded)
	{
		openWhenLoaded = false;
		done.openDialogUnlessEmbedded = instanceLoaded;
	}
	return done;
}
