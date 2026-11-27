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

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

// After windows.h so the Velopack C ABI header sees the platform headers in order.
#include <Velopack.hpp>

#include "CustomStyle.h"
#include "EditorGates.h"
#include "MainWindow.h"
#include "SkinManager.h"
#include "import/LegacyMigration.h"
#include "vst/PluginResidency.h"
#include "services/logging/Logging.h"
#include "services/logging/TaggedLogger.h"
#include "platform/windows/CommandLineQuoting.h"
#include "services/app/ProcessShutdown.h"
#include "services/install/ApoRegistration.h"
#include "services/windows/AudioEngineRestart.h"
#include "services/registry/WindowsRegistry.h"
#include "platform/windows/Win32Resource.h"
#include "platform/windows/WindowsPath.h"
#include "platform/windows/ElevatedLaunch.h"
#include "services/install/InstallWaitBudget.h"
#include "services/install/VcRuntime.h"
#include "services/security/AudioEngineAccess.h"
#include "services/diagnostics/InstallDiagnostics.h"
#include "services/update/UpdateLeftovers.h"
#include "services/update/UpdateSession.h"
#include "services/update/VelopackBootstrap.h"
#include "dsp/FftwPlanningPolicy.h"
#include "version.h"
#include "services/app/QtAppBootstrap.h"
#include "Editor/helpers/CrashHandler.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/helpers/BackgroundProbe.h"
#include "Editor/helpers/PluginLoadQueue.h"
#include "Editor/helpers/PluginUiThreadBridge.h"
#include <QScopeGuard>
#include "Editor/helpers/SpinBoxEditFieldFix.h"
#include "services/settings/EditorSettings.h"
#include "Editor/skins/SkinThemeData.h"


namespace
{
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

bool hasEditorGateArgument(int argc, const char* const argv[])
{
	const QStringList gateFlags = EditorGates::flags();
	for (int i = 1; i < argc; i++)
	{
		if (argv[i] == nullptr)
			continue;
		const QString argument = QString::fromLatin1(argv[i]);
		for (const QString& flag : gateFlags)
		{
			if (argument == flag || argument.startsWith(flag + QLatin1Char('=')))
				return true;
		}
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

constexpr logging::TaggedLogger logLine(L"Editor");

// This process's arguments after the program name, as the command line
// carried them. The CRT's argv is in the ANSI code page, so a path it could
// not represent was already lost before any widening (audit #348 TD-53).
std::vector<std::wstring> wideArgumentsAfterProgramName()
{
	std::vector<std::wstring> arguments;
	int argc = 0;
	winutil::UniqueLocalPtr<wchar_t*> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
	if (!argv)
		return arguments;
	for (int i = 1; i < argc; i++)
		arguments.push_back(argv.get()[i]);
	return arguments;
}

// Re-launches this exe elevated with the same arguments, waits, and returns
// the child's exit code. Per-user setup/uninstall can invoke hooks in the
// user's security context, while APO registration needs HKLM access. The
// Editor's in-app update path elevates Update.exe once before either update
// hook, so this per-hook fallback is not reached during that flow.
int relaunchElevatedAndWait()
{
	std::wstring exePath = pathutil::exePath();
	if (exePath.empty())
	{
		logLine(L"ERR", L"GetModuleFileName failed (gle=%lu)", GetLastError());
		return 1;
	}

	auto arguments = wideArgumentsAfterProgramName();
	// Last occurrence wins on the elevated side, so an incoming value cannot
	// override the environment of the process actually requesting elevation.
	if (std::find(arguments.begin(), arguments.end(), L"--veloapp-install") != arguments.end()
		|| std::find(arguments.begin(), arguments.end(), L"--veloapp-updated") != arguments.end())
	{
		EqAPO::Import::LegacyMigration::Handoff details;
		const auto outcome = EqAPO::Import::LegacyMigration::prepareHookStep(pathutil::exeDirectory(), &details);
		logLine(L"INFO", L"Unelevated migration preparation: %s", outcome.c_str());
		arguments.push_back(L"--caller-localappdata");
		arguments.push_back(pathutil::environmentVariable(L"LOCALAPPDATA"));
		arguments.push_back(L"--caller-migration-outcome");
		arguments.push_back(outcome);
		arguments.push_back(L"--caller-migrated-from");
		arguments.push_back(details.migratedFrom);
		arguments.push_back(L"--caller-migrated-files");
		arguments.push_back(details.migratedFiles);
		arguments.push_back(L"--caller-install-grants-prepared");
		arguments.push_back(details.installGrantsPrepared ? L"1" : L"0");
	}
	std::wstring parameters = winutil::joinCommandLineArguments(arguments);

	auto launch = winutil::launchElevated(exePath, parameters, true);
	if (!launch.started)
	{
		logLine(L"ERR", L"ShellExecuteEx(runas) failed (gle=%lu)", launch.error);
		return launch.error == ERROR_CANCELLED ? 1223 : 1;
	}
	if (!launch.process)
		return 1;
	if (WaitForSingleObject(launch.process.get(), installwait::remaining(INFINITE)) != WAIT_OBJECT_0)
	{
		logLine(L"ERR", L"Elevated install hook wait reached its deadline or failed");
		return 1;
	}
	DWORD exitCode = 1;
	GetExitCodeProcess(launch.process.get(), &exitCode);
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

	// Only the elevated child spends the 12 s service/ACL budget. Time the
	// user takes to answer UAC must not exhaust it (audit #467 TD-64).
	const bool elevated = AudioEngineAccess::isElevated();
	installwait::Budget waitBudget(12000, elevated);
	if (!elevated)
		return relaunchElevatedAndWait();

	const auto callerLocalAppData = EqAPO::Import::LegacyMigration::parseHandoff(wideArgumentsAfterProgramName());

	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		if (arg == nullptr || arg[0] != '-')
			continue;

		std::wstring exeDir = pathutil::exeDirectory();
		if (matchesHook(arg, "--veloapp-install") || matchesHook(arg, "--veloapp-updated"))
			vcruntime::ensureInstalled(exeDir);
		if (matchesHook(arg, "--veloapp-install"))
		{
			auto rc = ApoRegistration::install(exeDir, L"", systemRegistry(), callerLocalAppData.installGrantsPrepared);
			// The trusted config root: adopt the stable folder, or migrate a
			// legacy Equalizer APO / volatile current\config tree into it.
			if (rc == ApoRegistration::Result::AclFailed)
				logLine(L"WARN", L"Install completed with an access grant failure");
			if (ApoRegistration::hookCanComplete(rc))
				EqAPO::Import::LegacyMigration::runElevatedHookStep(exeDir, callerLocalAppData);
			return ApoRegistration::hookCanComplete(rc) ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-updated"))
		{
			ApoRegistration::AudioEngineState audioState;
			// The obsolete hook in our previous version may already have
			// stopped it. This update still owes that restart (audit #467 TD-13).
			audioState.resumePreviousHook = true;
			const auto rc = ApoRegistration::runWithAudioStopped([&] {
				return ApoRegistration::stopAudioEngine(audioState);
			}, [&] {
				const auto result = ApoRegistration::install(exeDir, L"", systemRegistry(), callerLocalAppData.installGrantsPrepared);
				if (result == ApoRegistration::Result::AclFailed)
					logLine(L"WARN", L"Update completed with an access grant failure");
				if (ApoRegistration::hookCanComplete(result))
					EqAPO::Import::LegacyMigration::runElevatedHookStep(exeDir, callerLocalAppData);
				return result;
			}, [&] { return ApoRegistration::startAudioEngine(audioState); });
			return ApoRegistration::hookCanComplete(rc) ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-obsolete"))
		{
			// Stop the endpoint builder and its dependents so audiodg releases the
			// DLL. Audio returns in the new version's --veloapp-updated hook.
			ApoRegistration::AudioEngineState audioState;
			ApoRegistration::stopAudioEngine(audioState);
			return 0;
		}
		if (matchesHook(arg, "--veloapp-uninstall"))
		{
			// The device sweep reports per-item failures and does not throw
			// them, and uninstall() restarts the audio service on every path;
			// this guard keeps anything else from ending the hook in the crash
			// handler instead of with an exit code (audit #348 TD-02).
			try
			{
				auto rc = ApoRegistration::uninstall(exeDir);
				return ApoRegistration::hookCanComplete(rc) ? 0 : static_cast<int>(rc);
			}
			catch (const RegistryError& e)
			{
				logLine(L"ERR", L"uninstall hook failed: %s", e.getMessage().c_str());
			}
			catch (const std::exception& e)
			{
				logLine(L"ERR", L"uninstall hook failed: %S", e.what());
			}
			return static_cast<int>(ApoRegistration::Result::DeviceUninstallFailed);
		}
	}
	return -1;
}

int shutdownEditor(UpdateSession* updateSession, bool headlessGateRan, int result)
{
	// Mark unfinished downloads before any shutdown wait: a slow plug-in or
	// probe must not let a just-closed window publish an update (audit #467 TD-64).
	if (updateSession)
		updateSession->cancelPendingDownload();

	// The SDK cannot cancel a download. Five seconds gives a finishing request
	// time to return without letting the network keep a closed Editor alive.
	constexpr unsigned long kUpdateShutdownTimeoutMs = 5000;
	ProcessShutdown::exitCode.store(result, std::memory_order_relaxed);
	// A timeout skips apply and leaves a staged package on disk. Startup auto-apply
	// is disabled; the next normal session retries CheckForUpdates/DownloadUpdates
	// after 60 s. Velopack 1.1.1 compares against the installed version and reuses
	// an existing final package, so it can apply on that session's normal exit
	// if the feed still offers it. This is not an offline startup-apply promise.
	// testShutdownSequenceTimeoutSkipsApplyAndLaterWaits, audit #467 TD-58.
	ProcessShutdown::run({
		// A plug-in load may still be running on the loader thread (closing the
		// Editor during a slow first load). Let it finish before static
		// destruction instead of racing it.
		{ PluginLoadQueue::shutdownWorkName, PluginLoadQueue::shutdown },
		// The same for an endpoint format probe still running.
		{ [] { return L"background probe"; }, BackgroundProbe::shutdown },
		// The session owns the download worker. Join it before inspecting staged state so
		// neither process shutdown nor static destruction can race with its publication.
		{ [] { return L"update download worker"; }, [updateSession] {
			return updateSession == nullptr || updateSession->shutdown(kUpdateShutdownTimeoutMs);
		} }
	}, [&] {
		// Gates keep their original exit code and never apply a previously staged
		// product update, but they now leave through the shared worker shutdown.
		if (headlessGateRan)
			return;

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
				result = 0;
			if (outcome == UpdateApplyOutcome::Failed)
				logLine(L"ERR", L"staged update could not be applied");
		}
	});
	return result;
}

void launchDeviceSelector(const std::wstring& exeDir)
{
	const std::wstring deviceSelector = pathutil::joinPath(exeDir, L"DeviceSelector.exe");

	HINSTANCE result = ShellExecuteW(nullptr, L"open", deviceSelector.c_str(), L"/i", exeDir.c_str(), SW_SHOWNORMAL);
	// The Editor is a GUI-subsystem program, so the stderr line this used to
	// write went nowhere and a first run that never opened the Device Selector
	// left no trace (audit #348 TD-49).
	if (reinterpret_cast<INT_PTR>(result) <= 32)
	{
		const DWORD gle = GetLastError();
		logLine(L"ERR", L"DeviceSelector launch failed for %s (code=%lld, gle=%lu)", deviceSelector.c_str(),
			static_cast<long long>(reinterpret_cast<INT_PTR>(result)), gle);
	}
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
	QtAppBootstrap::installMessageHandler();

	int hookResult = handleVelopackHook(argc, argv);
	if (hookResult >= 0)
		return hookResult;

	// Every full row rebuild (skin or dark switch, undo, device change, paste)
	// destroys the rows that held a plugin, and a plugin that does seconds of
	// work while loading would then freeze the window again for each one. See
	// PluginResidency.h.
	PluginResidency::keepLoadedForProcessLifetime(true);

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
			logLine(L"ERR", L"the diagnostics report could not be written");
			return 1;
		}
		logLine(L"INFO", L"diagnostics written to %s", reportPath.c_str());
		return 0;
	}

	const auto restartArguments = wideArgumentsAfterProgramName();
	if (std::find(restartArguments.begin(), restartArguments.end(), audioengine::kRestartArgument) != restartArguments.end())
	{
		if (!AudioEngineAccess::isElevated())
			return 5;
		return audioengine::restartAudioEngine([](const std::wstring& message) {
			logLine(L"ERR", L"%s", message.c_str());
		}) ? 0 : 1;
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
	if (VelopackBootstrap::isVelopackInstall() && !hasEditorGateArgument(argc, argv))
	{
		const std::wstring installRoot = VelopackBootstrap::installRoot();
		// audiodg keeps the previous EqualizerAPO.dll mapped, and Velopack stops
		// deleting at that file. Retry every removable leftover on startup (#508).
		std::thread([installRoot]() {
			SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
			UpdateLeftovers::sweep(installRoot);
		}).detach();
	}

	int result = -1;
	bool headlessGateRan = false;
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
	// DeviceSelector.
	QtAppBootstrap::addExecutableRelativePluginPath();

	// High-DPI: let Qt scale the whole UI by the monitor's device pixel ratio,
	// and pin the logical DPI to 96 (AA_Use96Dpi) so the code's pixel values need
	// no DPI factor of their own; Qt's device pixel ratio is then the single
	// scaling source and we avoid double scaling (the GUIHelper::scale helpers
	// were identities under this and are gone, audit #348 F7). PassThrough keeps
	// fractional factors like 150%
	// exact instead of rounding them to 100%/200%.
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	QCoreApplication::setAttribute(Qt::AA_Use96Dpi);

	bool restart = false;
	// Velopack reports the first run for the whole process, so an in-process
	// restart (language or interface-mode switch) of the first session used
	// to open Device Selector a second time (audit #348).
	bool firstRunHandled = false;
	do
	{
		ProcessShutdown::exitCode.store(1, std::memory_order_relaxed);
		// LegacyEditor is a whole presentation, not just a row widget: the
		// heritage editor runs with the platform's native widget style, the
		// stock ClearType font engine, and system fonts. The skinned mode
		// keeps the redesign stack below. Read the mode before QApplication
		// so the font-engine choice follows an in-process restart.
		bool legacyEditorMode = false;
		if (!hasEditorGateArgument(argc, argv))
		{
			QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
			legacyEditorMode = EditorSettings::readListMode(settings) == EditorSettings::ListMode::LegacyEditor;
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
		if (!legacyEditorMode && (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") || platformEnvSetByUs))
		{
			qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
			platformEnvSetByUs = true;
		}
		else if (legacyEditorMode && (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") || platformEnvSetByUs))
		{
			// The stock ClearType engine for the heritage look (also when
			// restarted from the skinned mode), and Windows dark mode off:
			// heritage draws its rows and menus with classic light tokens, but
			// Qt's Windows platform follows the system colour scheme, so in
			// dark mode applyHeritage's standardPalette() turned the text white
			// on those light surfaces (#484).
			qputenv("QT_QPA_PLATFORM", "windows:darkmode=0");
			platformEnvSetByUs = true;
		}

		QApplication application(argc, argv);
		PluginUiThreadBridge::install();
		const auto pluginUiThreadCleanup = qScopeGuard([] { PluginUiThreadBridge::uninstall(); });
		// Before anything can load a plug-in; see PluginLoadQueue.h.
		PluginLoadQueue::prepareUiThread();
		// Qt 6.12.0 only: spin box text placement (QTBUG-151308).
		SpinBoxEditFieldFix::installIfAffected(application);
		if (!legacyEditorMode)
		{
			application.setStyle(new CustomStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
			SkinThemeData::registerBundledFonts(true);
		}

		// Headless gates run before the registry skin and translator setup on
		// purpose. The gallery applies each skin itself and renders untranslated
		// English strings for deterministic output.
		if (const std::optional<int> gateResult = EditorGates::runHeadless(application.arguments()))
		{
			result = *gateResult;
			ProcessShutdown::exitCode.store(result, std::memory_order_relaxed);
			headlessGateRan = true;
			restart = false;
			continue;
		}

		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		if (legacyEditorMode)
		{
			SkinManager::instance()->applyHeritage();
		}
		else
		{
			const EditorSettings::SkinChoice choice = EditorSettings::readSkinChoice(settings, GUIHelper::isSystemDarkMode());
			// applySkin also derives the application palette from the tokens.
			SkinManager::instance()->applySkin(choice.id, choice.dark);
		}

		QtAppBootstrap::applyUserLocale();

		QTranslator qtTranslator;
		QTranslator editorTranslator;
		QtAppBootstrap::installTranslators(application, QStringLiteral("Editor"), qtTranslator, editorTranslator);

		// HKLM ConfigPath, else the stable XT config root, else the working
		// directory: the same rule the file dialog's sidebar uses.
		QDir configDir(EqAPO::Import::LegacyMigration::configRoot(systemRegistry()));

		if (!systemRegistry().keyExists(USER_REGPATH))
			systemRegistry().createKey(USER_REGPATH);

		if (!systemRegistry().keyExists(EDITOR_REGPATH))
			systemRegistry().createKey(EDITOR_REGPATH);

		if (!systemRegistry().keyExists(EDITOR_PER_FILE_REGPATH))
			systemRegistry().createKey(EDITOR_PER_FILE_REGPATH);

		// The live-window probes marked for a test window ignore saved geometry,
		// dock layout and open files, then load only the positional config.
		const bool testWindowRequested = EditorGates::wantsTestWindow(application.arguments());
		// The constructor reopens tabs and queues their plug-ins; the loader
		// waits until the shown window has UI Automation running.
		PluginLoadQueue::holdLoads();
		MainWindow w(configDir, updateSession.get(), nullptr, testWindowRequested);
		w.show();
		PluginLoadQueue::startUiAutomation(w.winId());
		PluginLoadQueue::releaseLoads();

		// One-time notice after the install hook migrated a config tree; a
		// no-op for everyone else.
		EqAPO::Import::LegacyMigration::maybeShowStartupNotice(&w);

		QCommandLineParser parser;
		EditorGates::registerOptions(parser);
		parser.process(application);
		QStringList args = parser.positionalArguments();
		if (!testWindowRequested && args.isEmpty() && w.isEmpty())
			args = QStringList("config.txt");

		// The plug-in load probe starts measuring here, before the positional
		// load. EditorGates defers the other armed probes until that load is done.
		const bool probeArmed = EditorGates::arm(w, parser);
		for (const QString& arg : args)
			w.load(configDir.absoluteFilePath(arg));

		const bool firstRun = !firstRunHandled && VelopackBootstrap::isFirstRun();
		if (probeArmed)
		{
			// Probe sessions skip doChecks: a modal warning would stall their
			// timers.
		}
		else if (firstRun)
		{
			launchDeviceSelector(pathutil::exeDirectory());
			firstRunHandled = true;
		}
		else
			w.doChecks();

		if (shouldCheckAutomatically(EditorSettings::readAutomaticUpdates(settings), firstRun, updateSession != nullptr))
		{
			// Defer the background download so it does not race with audio service
			// work or a Device Selector launch right after the Editor opens.
			// 60s is long enough that the initial GUI paint, config load, and
			// device enumeration are all comfortably finished. The download runs on
			// its own worker thread and just stages the update for apply-on-exit.
			QTimer::singleShot(60000, qApp, [session = updateSession.get()]() {
				// Respect a choice made from the menu during the launch delay too.
				QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
				if (shouldCheckAutomatically(EditorSettings::readAutomaticUpdates(settings), false, session != nullptr))
					session->startDownload();
			});
		}

		result = application.exec();
		ProcessShutdown::exitCode.store(result, std::memory_order_relaxed);

		restart = w.shouldRestart();
		if (!restart && updateSession)
			updateSession->cancelPendingDownload();
	}
	while (restart);

	return shutdownEditor(updateSession.get(), headlessGateRan, result);
}
