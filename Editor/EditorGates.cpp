/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorGates.h"

#include <cstdio>
#include <share.h>
#include <string>
#include <utility>

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include "Editor/MainWindow.h"
#include "Editor/SkinGallery.h"
#include "Editor/diagnostics/FrameHitTestProbe.h"
#include "Editor/diagnostics/SkinSwitchStorm.h"
#include "Editor/helpers/CrashHandler.h"
#include "Editor/import/LegacyMigration.h"
#include "services/logging/Logging.h"

namespace
{
FILE* openHeadlessLog()
{
	const QString requestedPath = qEnvironmentVariable("EAPO_GATE_LOG");
	FILE* stream = nullptr;
	if (!requestedPath.isEmpty())
	{
		const std::wstring path = requestedPath.toStdWString();
		stream = _wfsopen(path.c_str(), L"wt, ccs=UTF-8", _SH_DENYWR);
	}
	if (stream == nullptr)
		stream = _wfsopen(L"NUL", L"wt, ccs=UTF-8", _SH_DENYWR);
	return stream;
}

void redirectHeadlessLog()
{
	// A list-rebuild gate can emit thousands of lines. Keep one stream for the
	// process so gates neither reopen Editor.log per line nor write user data.
	static FILE* stream = openHeadlessLog();
	if (stream != nullptr)
		Logging::useStream(stream, true, false, false);
	else
		Logging::useFile(L"NUL", true, false, false);
}

enum class Stage
{
	Headless,
	Armed
};

using HeadlessRunner = int (*)(const QStringList& arguments);
using ArmRunner = void (*)(MainWindow& window, const QCommandLineParser& parser);

struct GateEntry
{
	const char* flag;
	Stage stage;
	const char* valueName;
	bool testWindow;
	HeadlessRunner runHeadless;
	ArmRunner arm;
};

int runScrollBench(const QStringList&)
{
	return SkinGallery::runScrollBench();
}

int runPowerToggleTest(const QStringList&)
{
	return SkinGallery::runPowerToggleTest();
}

int runRoutingEditTest(const QStringList&)
{
	return SkinGallery::runRoutingEditTest();
}

int runCloseSaveTest(const QStringList&)
{
	return SkinGallery::runCloseSaveTest();
}

int runCloseSaveSweep(const QStringList& arguments)
{
	return SkinGallery::runCloseSaveSweep(arguments);
}

int runVstUntouchedTest(const QStringList&)
{
	return SkinGallery::runVstUntouchedTest();
}

int runVstSelfTest(const QStringList&)
{
	// Both halves always run so one log shows every loss at once.
	const int roundTrip = SkinGallery::runVstRoundTripSelfTest();
	const int fill = SkinGallery::runVstFillSelfTest();
	return (roundTrip != 0 || fill != 0) ? 1 : 0;
}

int runSkinGallery(const QStringList& arguments)
{
	return SkinGallery::run(arguments);
}

int runKnobSpecimen(const QStringList& arguments)
{
	return SkinGallery::runKnobSpecimen(arguments);
}

// Headless live skin-switch robustness gate (crash + slowness), with the
// offscreen contract of the gallery.
int runSkinSwitchTest(const QStringList& arguments)
{
	return SkinGallery::runSwitchTest(arguments);
}

// Headless card drag-move latency gate (the internal-drag commit path).
int runCardMoveTest(const QStringList& arguments)
{
	return SkinGallery::runCardMoveTest(arguments);
}

// Headless card pointer-selection gate. This exercises both ordinary header
// selection and clicks consumed by an editor control inside a card, then
// compares the model with the card chrome's live state.
int runCardSelectionTest(const QStringList& arguments)
{
	return SkinGallery::runCardSelectionTest(arguments);
}

// Read-only report of what the install hook's config migration would do on
// this machine (classification + manifest); writes nothing.
int runMigrationDryRun(const QStringList&)
{
	return EqAPO::Import::LegacyMigration::dryRun();
}

// Diagnostic self-test: crash deliberately so a field machine can verify
// that the crash handler leaves a dump + report under
// %LOCALAPPDATA%\EqualizerAPO\logs\crash.
int runCrashSelfTest(const QStringList&)
{
	CrashHandler::setBreadcrumb(L"selftest-crash");
	volatile int* fault = nullptr;
	// cppcheck-suppress nullPointer ; the dereference is the whole point of the self-test
	*fault = 1; // intentional access violation
	return 1;
}

int listGates(const QStringList&)
{
	for (const QString& flag : EditorGates::flags())
	{
		const QByteArray bytes = flag.toUtf8();
		std::fprintf(stdout, "%s\n", bytes.constData());
	}
	std::fflush(stdout);
	return 0;
}

template<typename Function>
void deferArm(MainWindow& window, Function function)
{
	// Plugin-load must start measuring before main loads the positional file.
	// The other probes used to arm immediately after that load, so defer them to
	// the first event-loop turn while main keeps one table-driven arm call.
	QTimer::singleShot(0, &window, [function = std::move(function)]() {
		if (!function())
			QCoreApplication::exit(1);
	});
}

void armPluginLoad(MainWindow& window, const QCommandLineParser&)
{
	if (!SkinGallery::armPluginLoadProbe(window))
		QTimer::singleShot(0, &window, []() { QCoreApplication::exit(1); });
}

// Deterministic real-window regression probe for the analysis dock. It runs
// the same right/bottom layout path as the position combo, records the live
// widget geometry and optionally captures the composed surface. The probe
// itself lives in gallery/GalleryProbes.cpp (audit #275 B7); it arms timers
// that later exit the event loop with the verdict.
void armAnalysisLayout(MainWindow& window, const QCommandLineParser& parser)
{
	const QString screenshot = parser.value(QStringLiteral("analysis-layout-test"));
	deferArm(window, [&window, screenshot]() {
		return SkinGallery::armAnalysisLayoutProbe(window, screenshot);
	});
}

void armAnalysisLatency(MainWindow& window, const QCommandLineParser&)
{
	const QStringList arguments = QCoreApplication::arguments();
	deferArm(window, [&window, arguments]() {
		return SkinGallery::armAnalysisLatencyProbe(window, arguments);
	});
}

// Real-window probe for the custom frame's hit testing under a native child
// cascade (diagnostics/FrameHitTestProbe).
void armFrameHitTest(MainWindow& window, const QCommandLineParser&)
{
	deferArm(window, [&window]() { return FrameHitTestProbe::arm(window); });
}

void armSkinMetrics(MainWindow& window, const QCommandLineParser&)
{
	deferArm(window, [&window]() { return SkinGallery::armSkinMetricsProbe(window); });
}

// Whole-window captures per skin/mode (SkinGallery::armWindowShotProbe).
void armWindowShot(MainWindow& window, const QCommandLineParser&)
{
	const QStringList arguments = QCoreApplication::arguments();
	deferArm(window, [&window, arguments]() {
		return SkinGallery::armWindowShotProbe(window, arguments);
	});
}

// Real-window A/B probe for the panel preview feed. Pair it with
// EAPO_DISABLE_PANEL_FEED=1 for the control run.
void armVstPanelFeed(MainWindow& window, const QCommandLineParser& parser)
{
	const QString duration = parser.value(QStringLiteral("vst-panel-feed-test"));
	deferArm(window, [&window, duration]() {
		return SkinGallery::armVstPanelFeedProbe(window, duration);
	});
}

// Diagnostic switch storm (see diagnostics/SkinSwitchStorm).
void armSkinSwitchStorm(MainWindow& window, const QCommandLineParser&)
{
	deferArm(window, [&window]() {
		SkinSwitchStorm::run(window);
		return true;
	});
}

// EditorOffscreenTest.Tests.ps1 reads these literals.
static const GateEntry kGates[] = {
	{
		"--scroll-bench",
		Stage::Headless,
		nullptr,
		false,
		runScrollBench,
		nullptr
	},
	{
		"--power-toggle-test",
		Stage::Headless,
		nullptr,
		false,
		runPowerToggleTest,
		nullptr
	},
	{
		"--routing-edit-test",
		Stage::Headless,
		nullptr,
		false,
		runRoutingEditTest,
		nullptr
	},
	{
		"--close-save-test",
		Stage::Headless,
		nullptr,
		false,
		runCloseSaveTest,
		nullptr
	},
	{
		"--close-save-sweep",
		Stage::Headless,
		"repeats",
		false,
		runCloseSaveSweep,
		nullptr
	},
	{
		"--vst-untouched-test",
		Stage::Headless,
		nullptr,
		false,
		runVstUntouchedTest,
		nullptr
	},
	{
		"--selftest-vst",
		Stage::Headless,
		nullptr,
		false,
		runVstSelfTest,
		nullptr
	},
	{
		"--skin-gallery",
		Stage::Headless,
		"outDir",
		false,
		runSkinGallery,
		nullptr
	},
	{
		"--skin-gallery-skins",
		Stage::Headless,
		"ids",
		false,
		nullptr,
		nullptr
	},
	{
		"--knob-specimen",
		Stage::Headless,
		"outDir",
		false,
		runKnobSpecimen,
		nullptr
	},
	{
		"--knob-specimen-skins",
		Stage::Headless,
		"ids",
		false,
		nullptr,
		nullptr
	},
	{
		"--skin-switch-test",
		Stage::Headless,
		nullptr,
		false,
		runSkinSwitchTest,
		nullptr
	},
	{
		"--card-move-test",
		Stage::Headless,
		nullptr,
		false,
		runCardMoveTest,
		nullptr
	},
	{
		"--card-selection-test",
		Stage::Headless,
		"outDir",
		false,
		runCardSelectionTest,
		nullptr
	},
	{
		"--migration-dry-run",
		Stage::Headless,
		nullptr,
		false,
		runMigrationDryRun,
		nullptr
	},
	{
		"--selftest-crash",
		Stage::Headless,
		nullptr,
		false,
		runCrashSelfTest,
		nullptr
	},
	{
		"--list-gates",
		Stage::Headless,
		nullptr,
		false,
		listGates,
		nullptr
	},
	{
		"--plugin-load-test",
		Stage::Armed,
		nullptr,
		true,
		nullptr,
		armPluginLoad
	},
	{
		"--analysis-layout-test",
		Stage::Armed,
		"screenshot",
		true,
		nullptr,
		armAnalysisLayout
	},
	{
		"--analysis-latency-test",
		Stage::Armed,
		"iterations",
		true,
		nullptr,
		armAnalysisLatency
	},
	{
		"--analysis-latency-device",
		Stage::Armed,
		"substring",
		false,
		nullptr,
		nullptr
	},
	{
		"--frame-hittest-test",
		Stage::Armed,
		nullptr,
		true,
		nullptr,
		armFrameHitTest
	},
	// Every real-window probe uses the test window, never saved user state (audit #467 TD-79).
	{
		"--skin-metrics-probe",
		Stage::Armed,
		nullptr,
		true,
		nullptr,
		armSkinMetrics
	},
	{
		"--window-shot",
		Stage::Armed,
		"outDir",
		true,
		nullptr,
		armWindowShot
	},
	// The window-shot sub-options are entries so the parser accepts them.
	{
		"--window-shot-skins",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--window-shot-modes",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--window-shot-dock",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--window-shot-width",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--window-shot-height",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--window-shot-dock-size",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--window-shot-select",
		Stage::Armed,
		"value",
		false,
		nullptr,
		nullptr
	},
	{
		"--vst-panel-feed-test",
		Stage::Armed,
		"durationMs",
		true,
		nullptr,
		armVstPanelFeed
	},
	{
		"--skin-switch-storm",
		Stage::Armed,
		nullptr,
		true,
		nullptr,
		armSkinSwitchStorm
	}
};

QString optionName(const GateEntry& entry)
{
	return QString::fromLatin1(entry.flag + 2);
}
}

std::optional<int> EditorGates::runHeadless(const QStringList& arguments)
{
	for (const GateEntry& entry : kGates)
	{
		if (entry.stage == Stage::Headless && entry.runHeadless != nullptr
			&& arguments.contains(QString::fromLatin1(entry.flag)))
		{
			redirectHeadlessLog();
			return entry.runHeadless(arguments);
		}
	}
	return std::nullopt;
}

bool EditorGates::wantsTestWindow(const QStringList& arguments)
{
	for (const GateEntry& entry : kGates)
	{
		if (entry.testWindow && arguments.contains(QString::fromLatin1(entry.flag)))
			return true;
	}
	return false;
}

void EditorGates::registerOptions(QCommandLineParser& parser)
{
	for (const GateEntry& entry : kGates)
	{
		if (entry.stage != Stage::Armed)
			continue;
		QCommandLineOption option(optionName(entry));
		if (entry.valueName != nullptr)
			option.setValueName(QString::fromLatin1(entry.valueName));
		option.setFlags(QCommandLineOption::HiddenFromHelp);
		parser.addOption(option);
	}
}

bool EditorGates::arm(MainWindow& window, const QCommandLineParser& parser)
{
	for (const GateEntry& entry : kGates)
	{
		if (entry.stage == Stage::Armed && entry.arm != nullptr
			&& parser.isSet(optionName(entry)))
		{
			entry.arm(window, parser);
			return true;
		}
	}
	return false;
}

QStringList EditorGates::flags()
{
	QStringList result;
	for (const GateEntry& entry : kGates)
		result.append(QString::fromLatin1(entry.flag));
	return result;
}
