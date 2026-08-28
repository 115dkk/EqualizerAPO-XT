/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <cstdio>
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"
#include "services/registry/RegistryPaths.h"
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include <QTranslator>
#include <QApplication>
#include <QBoxLayout>
#include <QDir>
#include <QCommandLineParser>
#include <QDockWidget>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

// After windows.h so the Velopack C ABI header sees the platform headers in order.
#include <Velopack.hpp>

#include <fftw3.h>

#include "CustomStyle.h"
#include "MainWindow.h"
#include "SkinGallery.h"
#include "diagnostics/SkinSwitchStorm.h"
#include "SkinManager.h"
#include "import/LegacyMigration.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "guis/VSTPluginFilterGUI.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"
#include "services/logging/Logging.h"
#include "runtime/memory/AlignedMemory.h"
#include "services/install/ApoRegistration.h"
#include "services/registry/WindowsRegistry.h"
#include "platform/windows/Win32Resource.h"
#include "services/security/AudioEngineAccess.h"
#include "services/diagnostics/InstallDiagnostics.h"
#include "services/update/UpdateSession.h"
#include "services/update/VelopackBootstrap.h"
#include "dsp/FftwPlanningPolicy.h"
#include "version.h"
#include "helpers/QtAppBootstrap.h"
#include "Editor/helpers/CrashHandler.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/helpers/EditorSettings.h"
#include "Editor/skins/SkinThemeData.h"


namespace
{
// The VST round-trip self test lives with the other offscreen gates in
// SkinGallery.cpp (audit #275 B7).

std::wstring executableDirectory()
{
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (length == 0)
		return std::wstring();
	std::wstring path(buffer, length);
	size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return path;
	return path.substr(0, slash);
}

bool matchesHook(const char* arg, const char* name)
{
	return std::strcmp(arg, name) == 0;
}

bool isHookArgument(const char* arg)
{
	return arg != nullptr && (
		matchesHook(arg, "--veloapp-install") ||
		matchesHook(arg, "--veloapp-updated") ||
		matchesHook(arg, "--veloapp-obsolete") ||
		matchesHook(arg, "--veloapp-uninstall"));
}

bool hasArgument(int argc, const char* const argv[], const char* expected)
{
	for (int i = 1; i < argc; i++)
	{
		if (argv[i] != nullptr && std::strcmp(argv[i], expected) == 0)
			return true;
	}
	return false;
}

std::string configuredUpdateChannel()
{
#ifdef EAPO_UPDATE_CHANNEL
	return EAPO_UPDATE_CHANNEL;
#else
	return std::string();
#endif
}

std::wstring widenArg(const char* arg)
{
	if (arg == nullptr)
		return std::wstring();
	return wintext::toWideString(std::string(arg), CP_UTF8);
}

std::wstring buildArgumentLine(int argc, char* argv[])
{
	std::wstring line;
	for (int i = 1; i < argc; i++)
	{
		std::wstring piece = widenArg(argv[i]);
		if (i > 1)
			line.push_back(L' ');
		bool needsQuote = piece.empty() || piece.find_first_of(L" \t\"") != std::wstring::npos;
		if (needsQuote)
		{
			line.push_back(L'"');
			for (wchar_t ch : piece)
			{
				if (ch == L'"')
					line.push_back(L'\\');
				line.push_back(ch);
			}
			line.push_back(L'"');
		}
		else
		{
			line += piece;
		}
	}
	return line;
}

// Re-launches this exe elevated with the same arguments, waits, and returns
// the child's exit code. Per-user setup/uninstall can invoke hooks in the
// user's security context, while APO registration needs HKLM access. The
// Editor's in-app update path elevates Update.exe once before either update
// hook, so this per-hook fallback is not reached during that flow.
int relaunchElevatedAndWait(int argc, char* argv[])
{
	wchar_t exePathBuffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
	if (length == 0)
	{
		LogFStatic(L"[Editor] GetModuleFileName failed (gle=%lu)", GetLastError());
		return 1;
	}

	std::wstring parameters = buildArgumentLine(argc, argv);

	SHELLEXECUTEINFOW info;
	ZeroMemory(&info, sizeof(info));
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	info.lpVerb = L"runas";
	info.lpFile = exePathBuffer;
	info.lpParameters = parameters.c_str();
	info.nShow = SW_HIDE;

	if (!ShellExecuteExW(&info))
	{
		DWORD gle = GetLastError();
		LogFStatic(L"[Editor] ShellExecuteEx(runas) failed (gle=%lu)", gle);
		// ERROR_CANCELLED (1223) means the user declined UAC.
		return gle == ERROR_CANCELLED ? 1223 : 1;
	}

	if (info.hProcess == nullptr)
		return 1;

	winutil::UniqueHandle process(info.hProcess);
	WaitForSingleObject(process.get(), INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(process.get(), &exitCode);
	return static_cast<int>(exitCode);
}

int handleVelopackHook(int argc, char* argv[])
{
	bool hookSeen = false;
	for (int i = 1; i < argc; i++)
	{
		if (isHookArgument(argv[i]))
		{
			hookSeen = true;
			break;
		}
	}
	if (!hookSeen)
		return -1;

	if (!AudioEngineAccess::isElevated())
		return relaunchElevatedAndWait(argc, argv);

	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		if (arg == nullptr || arg[0] != '-')
			continue;

		std::wstring exeDir = executableDirectory();
		if (matchesHook(arg, "--veloapp-install"))
		{
			auto rc = ApoRegistration::install(exeDir);
			// The trusted config root: adopt the stable folder, or migrate a
			// legacy Equalizer APO / volatile current\config tree into it.
			if (rc == ApoRegistration::Result::Success)
				EqAPO::Import::LegacyMigration::runElevatedHookStep(exeDir);
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-updated"))
		{
			ApoRegistration::stopAudioService();
			auto rc = ApoRegistration::install(exeDir);
			if (rc == ApoRegistration::Result::Success)
				EqAPO::Import::LegacyMigration::runElevatedHookStep(exeDir);
			ApoRegistration::startAudioService();
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-obsolete"))
		{
			ApoRegistration::stopAudioService();
			return 0;
		}
		if (matchesHook(arg, "--veloapp-uninstall"))
		{
			auto rc = ApoRegistration::uninstall(exeDir);
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
	}
	return -1;
}

void launchDeviceSelector(const std::wstring& exeDir)
{
	std::wstring deviceSelector = exeDir;
	if (!deviceSelector.empty() && deviceSelector.back() != L'\\' && deviceSelector.back() != L'/')
		deviceSelector.push_back(L'\\');
	deviceSelector += L"DeviceSelector.exe";

	HINSTANCE result = ShellExecuteW(nullptr, L"open", deviceSelector.c_str(), L"/i", exeDir.c_str(), SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		fwprintf(stderr, L"DeviceSelector launch failed (code=%lld)\n", static_cast<long long>(reinterpret_cast<INT_PTR>(result)));
}
}

int main(int argc, char* argv[])
{
	// First thing in the process: crashes must leave a minidump + breadcrumb
	// report behind.
	CrashHandler::install();

	// Before the hooks, not after. The hooks are the part of this program that
	// registers the APO, restarts the audio service and removes the APO from every
	// device, and they used to run before any log destination was chosen - so
	// their output landed in Logging's fallback, %TEMP%\EqualizerAPO.log. Under
	// elevation that %TEMP% belongs to whichever account the installer elevated
	// to, which is not the one the user would look in. The hook output now goes
	// where the rest of the Editor's does. It is still that account's
	// %LOCALAPPDATA% when elevated, but it is the same file the elevated update
	// coordinator writes, so there is one place to look rather than two.
	if (!Logging::useUserFile(L"Editor.log", true, false, false))
		Logging::useDefaultApoLog();

	int hookResult = handleVelopackHook(argc, argv);
	if (hookResult >= 0)
		return hookResult;

	// --diagnose before anything is built, so the report can be produced on a
	// machine where starting the Editor proper is part of the problem.
	//
	// It is offered here as well as in Device Selector because Device Selector
	// links with requireAdministrator: running it prompts for elevation even to
	// read, and this report exists precisely so someone can look before deciding
	// whether to change anything. The Editor runs as the user, so this is the form
	// to tell people about.
	if (hasArgument(argc, argv, "--diagnose"))
	{
		const std::wstring reportPath = InstallDiagnostics::writeReport();
		if (reportPath.empty())
		{
			LogFStatic(L"[Editor] the diagnostics report could not be written");
			return 1;
		}
		LogFStatic(L"[Editor] diagnostics written to %s", reportPath.c_str());
		return 0;
	}

	if (hasArgument(argc, argv, VelopackBootstrap::kElevatedCoordinatorArgument))
	{
		// The coordinator is an internal one-shot process, not a normal Editor
		// launch. Avoid VelopackApp's startup package scan and go directly to
		// reopening the already-staged update.
		return VelopackBootstrap::runElevatedUpdateCoordinator(
			EAPO_REPO_URL, configuredUpdateChannel());
	}

	// Initialise the Velopack runtime so UpdateManager resolves the correct
	// install context. Auto-apply-on-startup is off because we apply on exit instead.
	Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();
	std::unique_ptr<UpdateSession> updateSession = VelopackBootstrap::createUpdateSession(
		EAPO_REPO_URL,
		configuredUpdateChannel());

	int result = -1;
#ifdef _DEBUG
	// _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	// _CrtSetBreakAlloc(3318);
#endif

	// The FFTW planner keeps global mutable state and is NOT thread-safe. The
	// editor builds FFTW-using filters (Convolution, GraphicEQ) on the GUI
	// thread while AnalysisThread builds its own FilterEngine (and plans an FFT)
	// concurrently. Without this, the two planners race and corrupt FFTW's
	// global state, producing flaky start-up crashes (seen as access violations
	// in Qt layout code or abort()). This installs an internal lock so every
	// planner call across all threads is serialised. Must run once, before any
	// planning and before the analysis thread starts.
	FftwPlanningPolicy::ensurePlannerThreadSafe();

	// Anchor the Qt plugin search to the executable's directory; shared with
	// DeviceSelector and UpdateChecker.
	QtAppBootstrap::addExecutableRelativePluginPath();

	// High-DPI: let Qt scale the whole UI by the monitor's device pixel ratio,
	// and pin the logical DPI to 96 (AA_Use96Dpi) so GUIHelper::scale becomes a
	// no-op — Qt's device pixel ratio is then the single scaling source and we
	// avoid double scaling. PassThrough keeps fractional factors like 150%
	// exact instead of rounding them to 100%/200%.
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	QCoreApplication::setAttribute(Qt::AA_Use96Dpi);

	bool restart;
	do
	{
		// LegacyRows is a whole presentation, not just a row widget: the
		// heritage editor runs with the platform's native widget style, the
		// stock ClearType font engine, and system fonts. The skinned mode
		// keeps the redesign stack below. Read the mode before QApplication
		// so the font-engine choice follows an in-process restart.
		bool legacyRowsMode;
		{
			QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
			legacyRowsMode = settings.value(QLatin1String(EditorSettings::Keys::LegacyRows), false).toBool();
		}

		// Font rendering (skinned mode): force Qt's FreeType font engine on
		// Windows instead of the default DirectWrite/GDI ClearType subpixel
		// rasteriser. The bundled Pretendard ships as CFF/OTF, which ClearType
		// renders with subpixel colour fringing that reads as blur on low-PPI
		// monitors. FreeType uses grayscale antialiasing plus its own CFF
		// hinting, which stays consistent across monitors regardless of DPI.
		// Only set it when no platform is chosen externally (offscreen gallery
		// / CI must win) or when we set it ourselves on a previous loop pass.
		static bool platformEnvSetByUs = false;
		if (!legacyRowsMode && (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") || platformEnvSetByUs))
		{
			qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
			platformEnvSetByUs = true;
		}
		else if (legacyRowsMode && platformEnvSetByUs)
		{
			// Restarted from the skinned mode: hand the platform back to the
			// stock ClearType engine for the heritage look.
			qputenv("QT_QPA_PLATFORM", "windows");
		}

		QApplication application(argc, argv);
		if (!legacyRowsMode)
		{
			application.setStyle(new CustomStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
			SkinThemeData::registerBundledFonts(true);
		}

		if (application.arguments().contains(QStringLiteral("--scroll-bench")))
			return SkinGallery::runScrollBench();
		if (application.arguments().contains(QStringLiteral("--power-toggle-test")))
			return SkinGallery::runPowerToggleTest();
		if (application.arguments().contains(QStringLiteral("--routing-edit-test")))
			return SkinGallery::runRoutingEditTest();
		if (application.arguments().contains(QStringLiteral("--selftest-vst")))
		{
			// Both halves always run so one log shows every loss at once.
			const int roundTrip = SkinGallery::runVstRoundTripSelfTest();
			const int fill = SkinGallery::runVstFillSelfTest();
			return (roundTrip != 0 || fill != 0) ? 1 : 0;
		}

		// Headless screenshot gallery (skin program). Runs before the registry
		// skin/translator setup on purpose: the gallery applies each skin itself
		// and renders untranslated English strings for deterministic output.
		if (application.arguments().contains(QStringLiteral("--skin-gallery")))
			return SkinGallery::run(application.arguments());

		// Headless live skin-switch robustness gate (crash + slowness), same
		// offscreen contract as the gallery.
		if (application.arguments().contains(QStringLiteral("--skin-switch-test")))
			return SkinGallery::runSwitchTest(application.arguments());

		// Headless card drag-move latency gate (the internal-drag commit
		// path), same offscreen contract as the gallery.
		if (application.arguments().contains(QStringLiteral("--card-move-test")))
			return SkinGallery::runCardMoveTest(application.arguments());

		// Headless card pointer-selection gate. This exercises both ordinary
		// header selection and clicks consumed by an editor control inside a
		// card, then compares the model with the card chrome's live state.
		if (application.arguments().contains(QStringLiteral("--card-selection-test")))
			return SkinGallery::runCardSelectionTest(application.arguments());

		// Read-only report of what the install hook's config migration would
		// do on this machine (classification + manifest); writes nothing.
		if (application.arguments().contains(QStringLiteral("--migration-dry-run")))
			return EqAPO::Import::LegacyMigration::dryRun();

		// Diagnostic self-test: crash deliberately so a field machine can verify
		// that the crash handler leaves a dump + report under
		// %LOCALAPPDATA%\EqualizerAPO\logs\crash.
		if (application.arguments().contains(QStringLiteral("--selftest-crash")))
		{
			CrashHandler::setBreadcrumb(L"selftest-crash");
			volatile int* fault = nullptr;
			// cppcheck-suppress nullPointer ; the dereference is the whole point of the self-test
			*fault = 1; // intentional access violation
		}

		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		if (legacyRowsMode)
		{
			SkinManager::instance()->applyHeritage();
		}
		else
		{
			const EditorSettings::SkinChoice choice = EditorSettings::readSkinChoice(settings, GUIHelper::isDarkMode());
			// applySkin also derives the application palette from the tokens.
			SkinManager::instance()->applySkin(choice.id, choice.dark);
		}

		QtAppBootstrap::applyUserLocale();

		QTranslator qtTranslator;
		QTranslator editorTranslator;
		QtAppBootstrap::installTranslators(application, QStringLiteral("Editor"), qtTranslator, editorTranslator);

		// Without a registry value the old fallback was the process CWD, which
		// silently edits whatever folder the Editor was launched from; prefer
		// the stable XT config root when it exists.
		QString stableRoot = EqAPO::Import::LegacyMigration::stableConfigRoot();
		QString configPath = !stableRoot.isEmpty() && QDir(stableRoot).exists()
			? stableRoot : QDir::currentPath();
		if (systemRegistry().keyExists(APP_REGPATH) && systemRegistry().valueExists(APP_REGPATH, L"ConfigPath"))
			configPath = QString::fromStdWString(systemRegistry().readValue(APP_REGPATH, L"ConfigPath"));
		QDir configDir(configPath);

		if (!systemRegistry().keyExists(USER_REGPATH))
			systemRegistry().createKey(USER_REGPATH);

		if (!systemRegistry().keyExists(EDITOR_REGPATH))
			systemRegistry().createKey(EDITOR_REGPATH);

		if (!systemRegistry().keyExists(EDITOR_PER_FILE_REGPATH))
			systemRegistry().createKey(EDITOR_PER_FILE_REGPATH);

		const bool analysisLayoutTestRequested =
			application.arguments().contains(QStringLiteral("--analysis-layout-test"));
		MainWindow w(configDir, updateSession.get(), nullptr, analysisLayoutTestRequested);
		w.show();

		// One-time notice after the install hook migrated a config tree; a
		// no-op for everyone else.
		EqAPO::Import::LegacyMigration::maybeShowStartupNotice(&w);

		QCommandLineParser parser;
		// Diagnostic switch storm (see diagnostics/SkinSwitchStorm);
		// registered so the parser does not reject it as an unknown option.
		QCommandLineOption stormOption(QStringLiteral("skin-switch-storm"));
		stormOption.setFlags(QCommandLineOption::HiddenFromHelp);
		parser.addOption(stormOption);
		// Deterministic real-window regression probe for the analysis dock. This
		// runs the same right/bottom layout path as the position combo, records
		// the live widget geometry and optionally captures the composed surface.
		QCommandLineOption analysisLayoutOption(QStringLiteral("analysis-layout-test"));
		analysisLayoutOption.setValueName(QStringLiteral("screenshot"));
		analysisLayoutOption.setFlags(QCommandLineOption::HiddenFromHelp);
		parser.addOption(analysisLayoutOption);
		// Real-window A/B probe for the panel preview feed (SkinGallery::
		// armVstPanelFeedProbe); pair it with EAPO_DISABLE_PANEL_FEED=1 for
		// the control run.
		QCommandLineOption vstPanelFeedOption(QStringLiteral("vst-panel-feed-test"));
		vstPanelFeedOption.setValueName(QStringLiteral("durationMs"));
		vstPanelFeedOption.setFlags(QCommandLineOption::HiddenFromHelp);
		parser.addOption(vstPanelFeedOption);
		parser.process(application);
		QStringList args = parser.positionalArguments();
		if (!analysisLayoutTestRequested && args.isEmpty() && w.isEmpty())
			args = QStringList("config.txt");

		for (const QString& arg : args)
			w.load(configDir.absoluteFilePath(arg));

		bool firstRun = VelopackBootstrap::isFirstRun();
		if (parser.isSet(analysisLayoutOption))
		{
			// The probe itself lives with the other offscreen gates in
			// SkinGallery.cpp (audit #275 B7); it arms the timers and later
			// exits the event loop with the verdict.
			if (!SkinGallery::armAnalysisLayoutProbe(w, parser.value(analysisLayoutOption)))
				return 1;
		}
		else if (parser.isSet(vstPanelFeedOption))
		{
			// Skips doChecks like the storm: a modal warning would stall the
			// probe timers.
			if (!SkinGallery::armVstPanelFeedProbe(w, parser.value(vstPanelFeedOption)))
				return 1;
		}
		else if (parser.isSet(stormOption))
			SkinSwitchStorm::run(w);  // storm sessions skip doChecks: its modal warnings would stall the timer
		else if (firstRun)
			launchDeviceSelector(executableDirectory());
		else
			w.doChecks();

		if (updateSession && !firstRun)
		{
			// Defer the background download so it does not race with audio service
			// work or a Device Selector launch right after the Editor opens.
			// 60s is long enough that the initial GUI paint, config load, and
			// device enumeration are all comfortably finished. The download runs on
			// its own worker thread and just stages the update for apply-on-exit.
			QTimer::singleShot(60000, qApp, [session = updateSession.get()]() {
				session->startDownload();
			});
		}

		result = application.exec();

		restart = w.shouldRestart();
	}
	while (restart);

	// The session owns the download worker. Join it before inspecting staged state so
	// neither process shutdown nor static destruction can race with its publication.
	if (updateSession)
		updateSession->shutdown();

	// If the background worker staged an update, apply it now. exec() has returned and
	// the QApplication is destroyed, so no other thread is writing to the install dir.
	// The apply is silent and does not restart; the new version comes up next launch.
	if (updateSession && updateSession->hasPendingUpdate())
	{
		const UpdateApplyOutcome outcome = updateSession->applyPendingUpdate(
			AudioEngineAccess::isElevated(),
			[]() { return VelopackBootstrap::launchElevatedUpdateCoordinator(); });
		if (outcome == UpdateApplyOutcome::CoordinatorLaunched ||
			outcome == UpdateApplyOutcome::UpdaterLaunched)
		{
			return 0;
		}
		if (outcome == UpdateApplyOutcome::Failed)
			LogFStatic(L"[Editor] staged update could not be applied");
	}

	return result;
}
