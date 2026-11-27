/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	When VSTPluginSession reads a plug-in's state because the plug-in called
	automate. The call arrives from inside the plug-in's own code: a control's
	handler, a timer, even the getState the session is calling to read it. Some
	plug-ins make it many hundreds of times a second with nothing changed.
	Reading the state right there called getState from inside the plug-in's
	performEdit, and a plug-in that announces its parameters again from
	getState then sent the two calls back and forth until the stack ran out.
	So an automate call only asks for a read; the session reads between
	events, and no more often than minimumIntervalMs. It has no signals and no
	widgets, so EditorLogicTests pins it.
*/

#pragma once

class VSTAutomatedRead
{
public:
	// The shortest time between two reads that automate calls asked for.
	static constexpr int minimumIntervalMs = 100;

	// The session starts and ends its own read of the plug-in's state. An
	// automate call in between is the plug-in answering that read, not an
	// edit, and asks for nothing.
	void stateReadStarted()
	{
		++stateReads;
	}
	void stateReadEnded()
	{
		if (stateReads > 0)
			--stateReads;
	}

	// The plug-in called automate.
	void automateArrived()
	{
		if (stateReads == 0)
			wanted = true;
	}

	// Whether the session reads now. msSinceLastRead: the time since its last
	// read of any kind, negative when it has not read yet.
	bool readDue(long long msSinceLastRead) const
	{
		return wanted && (msSinceLastRead < 0 || msSinceLastRead >= minimumIntervalMs);
	}

	// The session is about to make the read that was asked for; it answers
	// every automate call that arrived before it.
	void readTaken()
	{
		wanted = false;
	}

private:
	int stateReads = 0;
	bool wanted = false;
};
