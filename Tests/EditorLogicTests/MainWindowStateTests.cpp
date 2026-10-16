/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The decision tables and the document state MainWindow writes into its
// widgets (audit #422 D4, D5), pinned without a window.

#include "EditorLogicTestSupport.h"

#include "Editor/MainWindowParts/OpenDocument.h"
#include "Editor/analysis/AnalysisPanelState.h"
#include "Editor/widgets/DeviceFormatBadgeState.h"

namespace
{
const QString dash = QString::fromUtf8("\xE2\x80\x94");

bool isSeverity(AnalysisPanelState::Severity actual, AnalysisPanelState::Severity expected)
{
	return actual == expected;
}
}

void testAnalysisPanelStateCoversEveryOutcome()
{
	using Severity = AnalysisPanelState::Severity;

	{
		AnalysisPanelFacts facts;
		facts.setupFailed = true;
		facts.setupErrorLine = 7;
		facts.setupErrorFileName = QStringLiteral("config.txt");
		facts.errorText = QStringLiteral("ignored while the load rolled back");
		facts.peakGain = 3.0;
		const AnalysisPanelState state = AnalysisPanelState::describe(facts);
		expectEqual(state.notice, QStringLiteral("This configuration was not applied: the filter on line 7 of config.txt "
			"could not be prepared. Equalizer APO keeps playing the previous settings."),
			"a setup error raises the window notice");
		expectTrue(state.clearGraph, "a configuration that does not play shows no curve");
		expectEqual(state.peakText, QStringLiteral("Not applied"), "the peak readout says not applied");
		expectEqual(state.peakToolTip, state.notice, "the peak readout carries the notice");
		expectTrue(isSeverity(state.peakSeverity, Severity::Critical), "not applied is critical");
		expectEqual(state.latencyText, dash, "no latency for a rolled-back load");
		expectEqual(state.initializationText, dash, "no initialization time for a rolled-back load");
		expectEqual(state.cpuText, dash, "no CPU figure for a rolled-back load");
		expectTrue(isSeverity(state.cpuSeverity, Severity::Normal), "the missing CPU figure is quiet");
		expectFalse(state.latencyToolTip.has_value(), "the latency tooltip is left alone");
	}

	{
		AnalysisPanelFacts facts;
		facts.errorText = QStringLiteral("device vanished");
		const AnalysisPanelState state = AnalysisPanelState::describe(facts);
		expectTrue(state.notice.isEmpty(), "a failed analysis hides the window notice");
		expectFalse(state.clearGraph, "a failed analysis keeps the response it published");
		expectEqual(state.peakText, QStringLiteral("Analysis failed"), "the peak readout says the analysis failed");
		expectEqual(state.peakToolTip, QStringLiteral("device vanished"), "the reason rides the tooltip");
		expectTrue(isSeverity(state.peakSeverity, Severity::Critical), "a failed analysis is critical");
		expectEqual(state.latencyText, dash, "no latency for a failed analysis");
		expectEqual(state.initializationText, dash, "no initialization time for a failed analysis");
		expectEqual(state.cpuText, dash, "no CPU figure for a failed analysis");
		expectFalse(state.latencyToolTip.has_value(), "the latency tooltip is left alone");
	}

	{
		AnalysisPanelFacts facts;
		facts.peakGain = -1.24;
		facts.initializationTime = 12.34;
		facts.sampleRate = 48000;
		facts.processedFrames = 0;
		const AnalysisPanelState state = AnalysisPanelState::describe(facts);
		expectTrue(state.notice.isEmpty() && !state.clearGraph, "a quiet result shows its curve");
		expectEqual(state.peakText, QStringLiteral("-1.2 dB"), "the peak gain with one decimal");
		expectTrue(state.peakToolTip.isEmpty(), "a quiet result clears the peak tooltip");
		expectTrue(isSeverity(state.peakSeverity, Severity::Normal), "a negative peak is normal");
		expectEqual(state.initializationText, QStringLiteral("12.3 ms"), "the initialization time");
		expectEqual(state.latencyText, dash, "no latency without a measured run");
		expectEqual(state.cpuText, dash, "no CPU figure without a measured run");
		expectTrue(isSeverity(state.cpuSeverity, Severity::Normal), "the missing CPU figure is quiet");
		expectFalse(state.latencyToolTip.has_value(), "the latency tooltip is left alone");
	}

	{
		AnalysisPanelFacts facts;
		facts.peakGain = 0.5;
		facts.initializationTime = 1.0;
		facts.sampleRate = 48000;
		facts.processedFrames = 48000;
		facts.latencyFrames = 480;
		facts.processingTime = 100.0;
		AnalysisPanelState state = AnalysisPanelState::describe(facts);
		expectTrue(isSeverity(state.peakSeverity, Severity::Critical), "a positive peak gain is critical");
		expectEqual(state.latencyText, QStringLiteral("10.0 ms"), "latency in milliseconds");
		expectTrue(state.latencyToolTip.has_value(), "a measured run explains its latency");
		expectEqual(state.latencyToolTip.value_or(QString()),
			QStringLiteral("10.0 ms (480 samples) of latency the analyzer removed before measuring"),
			"the latency tooltip carries the sample count");
		expectEqual(state.cpuText, QStringLiteral("10.0 %"), "CPU load of 100 ms per second");
		expectTrue(isSeverity(state.cpuSeverity, Severity::Normal), "below 20 % is normal");

		facts.processingTime = 200.0;
		state = AnalysisPanelState::describe(facts);
		expectTrue(isSeverity(state.cpuSeverity, Severity::Warning), "20 % is a warning");
		facts.processingTime = 500.0;
		state = AnalysisPanelState::describe(facts);
		expectTrue(isSeverity(state.cpuSeverity, Severity::Critical), "50 % is critical");
	}

	expectEqual(QString::fromLatin1(AnalysisPanelState::severityName(Severity::Normal)), QStringLiteral("normal"),
		"the skins style normal");
	expectEqual(QString::fromLatin1(AnalysisPanelState::severityName(Severity::Warning)), QStringLiteral("warning"),
		"the skins style warning");
	expectEqual(QString::fromLatin1(AnalysisPanelState::severityName(Severity::Critical)), QStringLiteral("critical"),
		"the skins style critical");
}

void testDeviceFormatBadgeCoversEveryRow()
{
	const QString dot = QString::fromUtf8("\xC2\xB7");

	{
		const DeviceFormatBadgeState badge = DeviceFormatBadgeState::describe(DeviceFormatFacts());
		expectFalse(badge.visible, "no device: no badge");
		expectTrue(badge.text.isEmpty(), "a hidden badge drops its text");
	}

	{
		DeviceFormatFacts facts;
		facts.deviceSelected = true;
		facts.asio = true;
		facts.asioSampleRate = 48000;
		facts.asioChannelCount = 2;
		DeviceFormatBadgeState badge = DeviceFormatBadgeState::describe(facts);
		expectTrue(badge.visible, "an ASIO interface shows its last stream");
		expectEqual(badge.text, QStringLiteral("ASIO %1 48000 Hz %1 2 ch").arg(dot), "the ASIO stream format");
		expectEqual(badge.toolTip, QStringLiteral("The last ASIO stream on this interface ran at 48000 Hz with 2 channels "
			"in this direction; the engine host processes it in a separate process."), "the ASIO tooltip");
		expectEqual(badge.severity, QStringLiteral("normal"), "an ASIO stream is normal");

		facts.asioSampleRate = 0;
		facts.asioDeviceName = QStringLiteral("Focusrite USB ASIO");
		badge = DeviceFormatBadgeState::describe(facts);
		expectEqual(badge.text, QStringLiteral("ASIO %1 no stream yet").arg(dot), "an ASIO interface before its first stream");
		expectEqual(badge.toolTip, QStringLiteral("No ASIO application has opened this interface through EqualizerAPO yet. "
			"Pick \"Focusrite USB ASIO (EQ APO XT)\" as the ASIO driver in the application."),
			"the tooltip names the driver to pick");
	}

	{
		DeviceFormatFacts facts;
		facts.deviceSelected = true;
		facts.probe.status = AudioFormatProbe::Status::ActiveFloat32;
		facts.probe.sampleRate = 44100;
		facts.probe.channelCount = 2;
		DeviceFormatBadgeState badge = DeviceFormatBadgeState::describe(facts);
		expectTrue(badge.visible, "a native float32 stream shows");
		expectEqual(badge.text, QStringLiteral("EQ active %1 32-bit float").arg(dot), "float32 label");
		expectEqual(badge.toolTip, QStringLiteral("EqualizerAPO is processing this stream natively (IEEE_FLOAT 32-bit, 44100 Hz, 2 ch)."),
			"float32 tooltip");
		expectEqual(badge.severity, QStringLiteral("normal"), "native processing is normal");

		facts.probe.status = AudioFormatProbe::Status::ActiveFloat64;
		badge = DeviceFormatBadgeState::describe(facts);
		expectEqual(badge.text, QStringLiteral("EQ active %1 64-bit float").arg(dot), "float64 label");
		expectEqual(badge.toolTip, QStringLiteral("EqualizerAPO is processing this stream natively (IEEE_FLOAT 64-bit, 44100 Hz, 2 ch)."),
			"float64 tooltip");

		facts.probe.status = AudioFormatProbe::Status::Passthrough;
		facts.probe.subtypeDescription = L"PCM";
		facts.probe.containerBytes = 3;
		badge = DeviceFormatBadgeState::describe(facts);
		expectTrue(badge.visible, "a passthrough stream shows");
		expectEqual(badge.text, QStringLiteral("Passthrough %1 EQ inactive").arg(dot), "passthrough label");
		expectTrue(badge.toolTip.startsWith(QStringLiteral("This device's stream format is PCM (24-bit container).")),
			"the passthrough tooltip names the format");
		expectEqual(badge.severity, QStringLiteral("warning"), "passthrough is a warning");

		facts.probe.status = AudioFormatProbe::Status::Unknown;
		badge = DeviceFormatBadgeState::describe(facts);
		expectFalse(badge.visible, "an endpoint that could not be queried hides the badge");
		expectTrue(badge.text.isEmpty(), "and drops its text");
	}

	// While a probe runs (audit #422 TD-04): the badge never shows one
	// endpoint's format while another endpoint is selected.
	{
		const QString endpointA = QStringLiteral("{0.0.0.00000000}.{aaaaaaaa-0000-0000-0000-000000000001}");
		const QString endpointB = QStringLiteral("{0.0.0.00000000}.{bbbbbbbb-0000-0000-0000-000000000002}");

		expectFalse(DeviceFormatBadgeState::whileProbing(endpointA, endpointA).has_value(),
			"a re-probe of the endpoint the badge describes keeps the badge");

		std::optional<DeviceFormatBadgeState> pending = DeviceFormatBadgeState::whileProbing(endpointA, endpointB);
		expectTrue(pending.has_value(), "a probe of another endpoint replaces the badge");
		expectTrue(pending.has_value() && !pending->visible && pending->text.isEmpty(),
			"with the empty state until its result arrives");

		pending = DeviceFormatBadgeState::whileProbing(QString(), endpointA);
		expectTrue(pending.has_value() && !pending->visible && pending->text.isEmpty(),
			"a badge that describes no endpoint (none, ASIO, or already empty) shows the empty state");
	}
}

void testDocumentStateOwnsTitleAndDirtyFlag()
{
	DocumentState document(QStringLiteral("config.txt"), QStringLiteral("C:\\EqualizerAPO\\config\\config.txt"));
	expectFalse(document.dirty(), "a loaded document starts saved");
	expectEqual(document.tabText(), QStringLiteral("config.txt"), "a saved tab shows the title alone");
	expectEqual(document.path(), QStringLiteral("C:\\EqualizerAPO\\config\\config.txt"), "the path is the tooltip");

	expectFalse(document.edited(false, true), "without instant mode an edit is not saved at once");
	expectTrue(document.dirty(), "the edit marks the document unsaved");
	expectEqual(document.tabText(), QStringLiteral("config.txt*"), "an unsaved tab carries one '*'");
	document.edited(false, true);
	expectEqual(document.tabText(), QStringLiteral("config.txt*"), "a second edit adds no second '*'");

	document.setDirty(false);
	expectEqual(document.tabText(), QStringLiteral("config.txt"), "saving takes the '*' away");

	expectTrue(document.edited(true, true), "instant mode saves an edit through the debounce");
	expectFalse(document.dirty(), "an instantly saved edit leaves the tab without '*'");
	expectFalse(document.edited(true, false), "a new document cannot be saved instantly");
	expectTrue(document.dirty(), "so the edit marks it unsaved");

	document.savedAs(QStringLiteral("other.txt"), QStringLiteral("C:\\EqualizerAPO\\config\\other.txt"));
	expectFalse(document.dirty(), "saved under a new name, the document is saved");
	expectEqual(document.tabText(), QStringLiteral("other.txt"), "the tab shows the new name");
	expectEqual(document.path(), QStringLiteral("C:\\EqualizerAPO\\config\\other.txt"), "the tooltip shows the new path");

	DocumentState fresh(QStringLiteral("Unsaved"), QString());
	expectTrue(fresh.path().isEmpty(), "a new document has no path");
	expectTrue(DocumentState::instantSaveDebounceMs > 0, "instant saves coalesce edits");
}

void testRecentFilesRule()
{
	QStringList recent;
	for (int i = 0; i < 12; i++)
		RecentFiles::remember(recent, QStringLiteral("file%1.txt").arg(i));
	expectEqual(static_cast<int>(recent.size()), 10, "the list keeps ten entries");
	expectEqual(recent.first(), QStringLiteral("file11.txt"), "the newest comes first");
	expectEqual(recent.last(), QStringLiteral("file2.txt"), "the oldest beyond ten fall off");

	RecentFiles::remember(recent, QStringLiteral("file5.txt"));
	expectEqual(static_cast<int>(recent.size()), 10, "remembering a listed file keeps the size");
	expectEqual(recent.first(), QStringLiteral("file5.txt"), "a reopened file moves to the front");
	expectEqual(static_cast<int>(recent.count(QStringLiteral("file5.txt"))), 1, "without a duplicate");

	// Closing a tab that was never saved remembers nothing.
	RecentFiles::remember(recent, QString());
	expectEqual(static_cast<int>(recent.size()), 10, "an empty path does not enter the list");
	expectEqual(recent.first(), QStringLiteral("file5.txt"), "and leaves the order alone");
	expectFalse(recent.contains(QString()), "the list holds no empty entry");
	QStringList none;
	RecentFiles::remember(none, QString());
	expectTrue(none.isEmpty(), "an empty path does not start a list either");
}
