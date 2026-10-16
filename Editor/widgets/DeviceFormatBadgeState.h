/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What the toolbar's device format badge says, as a decision table (audit
	#422 D4): nothing without a device, the last stream an ASIO interface ran
	(an ASIO target is not an endpoint, so the format probe cannot reach it),
	and for an endpoint whether the APO processes its mix format natively or
	passes it through. MainWindow gathers the facts (the endpoint probe runs
	off the UI thread) and writes the badge; the table has no widgets, so
	EditorLogicTests covers every row.
*/

#pragma once

#include <optional>

#include <QString>

#include "services/audio/AudioFormatProbe.h"

struct DeviceFormatFacts
{
	bool deviceSelected = false;
	bool asio = false;
	// ASIO: what the engine host published after the last stream (0 before
	// the first one), and the interface's name.
	unsigned asioSampleRate = 0;
	unsigned asioChannelCount = 0;
	QString asioDeviceName;
	// Endpoint: the probed mix format.
	AudioFormatProbe::Result probe;
};

struct DeviceFormatBadgeState
{
	// Hidden, the badge also drops its text; the tooltip and severity are
	// left as they are.
	bool visible = false;
	QString text;
	QString toolTip;
	// The value of the badge's "severity" property the skins style.
	QString severity;

	static DeviceFormatBadgeState describe(const DeviceFormatFacts& facts);

	// What the badge shows while the mix format of probedEndpoint is being
	// probed. describedEndpoint is the endpoint the badge's content describes
	// now (empty when it describes none: no device, an ASIO interface, or this
	// empty state itself). A re-probe of that endpoint keeps the badge
	// (nullopt); a probe of another endpoint shows the empty state until its
	// result arrives, so the badge never shows one device's format beside
	// another device's name.
	static std::optional<DeviceFormatBadgeState> whileProbing(const QString& describedEndpoint,
		const QString& probedEndpoint);
};
