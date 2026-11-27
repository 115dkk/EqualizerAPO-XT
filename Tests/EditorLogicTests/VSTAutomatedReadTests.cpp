/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTAutomatedRead.h"

// The Editor closed as soon as a control in zDolbi's panel was changed. The
// session read the plug-in's state from inside every automate call; zDolbi
// announces its parameters again from inside getState, so the read raised
// the next automate call and the two took turns until the stack ran out.
// Reading on every call was also too much on its own: zDolbi makes some nine
// hundred a second with nothing changed. An automate call now only asks for
// a read, and VSTPluginSession::onIdle makes it.
TEST_CASE(EditorLogicTests, testVSTAutomatedReadWaitsForTheSessionsTurn)
{
	{
		VSTAutomatedRead read;
		expectFalse(read.readDue(-1), "nothing is read for automate before the plug-in calls it");
		expectFalse(read.readWanted(), "no read waits before the plug-in calls automate");
		read.automateArrived();
		expectTrue(read.readDue(-1), "an automate call is read at the session's next turn");
		read.readTaken();
		expectFalse(read.readDue(60000), "one read answers every automate call that arrived before it");
	}

	{
		// A dialog that closes inside the minimum interval asks whether a
		// read still waits, so that the edit is not lost with the dialog.
		VSTAutomatedRead read;
		read.automateArrived();
		expectFalse(read.readDue(VSTAutomatedRead::minimumIntervalMs - 1), "the read is not due yet");
		expectTrue(read.readWanted(), "a read that is not due yet still waits");
		read.readTaken();
		expectFalse(read.readWanted(), "a read that was made no longer waits");
	}

	{
		VSTAutomatedRead read;
		for (int call = 0; call < 900; call++)
			read.automateArrived();
		expectFalse(read.readDue(VSTAutomatedRead::minimumIntervalMs - 1),
			"automate calls are not read sooner than the minimum interval after the last read");
		expectTrue(read.readDue(VSTAutomatedRead::minimumIntervalMs),
			"automate calls are read once the minimum interval has passed");
		read.readTaken();
		expectFalse(read.readDue(VSTAutomatedRead::minimumIntervalMs),
			"nine hundred automate calls ask for one read, not nine hundred");
	}

	{
		VSTAutomatedRead read;
		read.stateReadStarted();
		read.automateArrived();
		read.stateReadEnded();
		expectFalse(read.readDue(-1),
			"an automate call raised while the session reads the state asks for no further read");
		read.automateArrived();
		expectTrue(read.readDue(-1), "an automate call after that read asks for one");
	}

	{
		VSTAutomatedRead read;
		read.automateArrived();
		read.readTaken();
		read.stateReadStarted();
		read.automateArrived();
		read.stateReadEnded();
		expectFalse(read.readDue(60000),
			"the read an automate call asked for does not ask for itself again");
	}

	{
		VSTAutomatedRead read;
		read.stateReadStarted();
		read.stateReadStarted();
		read.stateReadEnded();
		read.automateArrived();
		expectFalse(read.readDue(-1), "a read inside a read still covers the automate calls it raises");
		read.stateReadEnded();
		read.stateReadEnded();
		read.automateArrived();
		expectTrue(read.readDue(-1), "one end too many does not leave the session deaf to automate");
	}
}
