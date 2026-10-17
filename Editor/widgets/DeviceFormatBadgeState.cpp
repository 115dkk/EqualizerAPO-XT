/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DeviceFormatBadgeState.h"

#include <QCoreApplication>

// The texts keep MainWindow's translation context, where the badge lives.
// The separator in the labels is U+00B7 MIDDLE DOT, spelled as its UTF-8
// bytes because the sources stay ASCII.
DeviceFormatBadgeState DeviceFormatBadgeState::describe(const DeviceFormatFacts& facts)
{
	DeviceFormatBadgeState state;
	if (!facts.deviceSelected)
		return state;

	state.severity = QStringLiteral("normal");
	if (facts.asio)
	{
		state.visible = true;
		if (facts.asioSampleRate > 0 && facts.asioChannelCount > 0)
		{
			state.text = QCoreApplication::translate("MainWindow", "ASIO \xC2\xB7 %0 Hz \xC2\xB7 %1 ch")
				.arg(facts.asioSampleRate).arg(facts.asioChannelCount);
			state.toolTip = QCoreApplication::translate("MainWindow", "The last ASIO stream on this interface ran at %0 Hz with %1 channels in this direction; "
				"the engine host processes it in a separate process.")
				.arg(facts.asioSampleRate).arg(facts.asioChannelCount);
		}
		else
		{
			state.text = QCoreApplication::translate("MainWindow", "ASIO \xC2\xB7 no stream yet");
			state.toolTip = QCoreApplication::translate("MainWindow", "No ASIO application has opened this interface through EqualizerAPO yet. "
				"Pick \"%0 (EQ APO XT)\" as the ASIO driver in the application.")
				.arg(facts.asioDeviceName);
		}
		return state;
	}

	const AudioFormatProbe::Result& r = facts.probe;
	switch (r.status)
	{
	case AudioFormatProbe::Status::ActiveFloat32:
		state.text = QCoreApplication::translate("MainWindow", "EQ active \xC2\xB7 32-bit float");
		state.toolTip = QCoreApplication::translate("MainWindow", "EqualizerAPO is processing this stream natively (IEEE_FLOAT 32-bit, %0 Hz, %1 ch).")
			.arg(r.sampleRate).arg(r.channelCount);
		break;
	case AudioFormatProbe::Status::ActiveFloat64:
		state.text = QCoreApplication::translate("MainWindow", "EQ active \xC2\xB7 64-bit float");
		state.toolTip = QCoreApplication::translate("MainWindow", "EqualizerAPO is processing this stream natively (IEEE_FLOAT 64-bit, %0 Hz, %1 ch).")
			.arg(r.sampleRate).arg(r.channelCount);
		break;
	case AudioFormatProbe::Status::Passthrough:
		state.text = QCoreApplication::translate("MainWindow", "Passthrough \xC2\xB7 EQ inactive");
		state.severity = QStringLiteral("warning");
		state.toolTip = QCoreApplication::translate("MainWindow", "This device's stream format is %0 (%1-bit container). EqualizerAPO only processes IEEE_FLOAT 32/64-bit streams natively, "
			"so audio is forwarded without any filter being applied. Switch the device's default format to a 32-bit IEEE_FLOAT one in "
			"Sound Settings if you need filtering on this device.")
			.arg(QString::fromStdWString(r.subtypeDescription)).arg(r.containerBytes * 8);
		break;
	case AudioFormatProbe::Status::Unknown:
	default:
		// The endpoint could not be queried: no claim either way.
		return DeviceFormatBadgeState();
	}
	state.visible = true;
	return state;
}

std::optional<DeviceFormatBadgeState> DeviceFormatBadgeState::whileProbing(const QString& describedEndpoint,
	const QString& probedEndpoint)
{
	if (!describedEndpoint.isEmpty() && describedEndpoint == probedEndpoint)
		return std::nullopt;
	return DeviceFormatBadgeState();
}
