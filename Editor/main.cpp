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
#include <cstring>
#include <string>

#include <QTranslator>
#include <QApplication>
#include <QDir>
#include <QCommandLineParser>
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
#include "SkinManager.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "guis/VSTPluginFilterGUI.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ApoRegistration.h"
#include "helpers/RegistryHelper.h"
#include "helpers/VelopackBootstrap.h"
#include "Editor/helpers/CrashHandler.h"
#include "Editor/helpers/GUIHelper.h"


namespace
{
// Mechanical round-trip check for VST plugin data: parse a VSTPlugin line, feed
// the parsed (library, chunkData, paramMap) into the real VSTPluginFilterGUI,
// call its store(), reparse the result and confirm chunkData / paramMap survive.
// Used to decide empirically whether a modern VST card editor (which would hold
// the same opaque state and reuse the same store logic) can replace the legacy
// GUI without losing plugin state. Returns 0 on success, 1 on any loss.
int runVstRoundTripSelfTest()
{
	struct Case { const wchar_t* name = nullptr; std::wstring params; };
	const Case cases[] = {
		{ L"chunkData", L"Library \"fake plugin.dll\" ChunkData \"QUJDREVGR0g=\"" },
		{ L"paramMap", L"Library fake.dll Gain 0.5 Mix 0.25 Width 1" },
		{ L"paramMap-quoted-name", L"Library fake.dll \"Dry/Wet\" 0.75 Output 0.5" }
	};

	int failures = 0;
	for (const Case& c : cases)
	{
		VSTPluginFilterFactory factory;
		std::wstring command = L"VSTPlugin";
		std::wstring params = c.params;
		std::vector<IFilter*> filters = factory.createFilter(L"", command, params);
		if (filters.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: parse produced no filter\n", c.name);
			failures++;
			continue;
		}
		VSTPluginFilter* f0 = static_cast<VSTPluginFilter*>(filters[0]);
		std::wstring chunk0 = f0->getChunkData();
		std::unordered_map<std::wstring, float> map0 = f0->getParamMap();

		// Real editor store() on the parsed state.
		VSTPluginFilterGUI gui(f0->getLibrary(), chunk0, map0);
		QString outCommand, outParams;
		gui.store(outCommand, outParams);

		for (IFilter* f : filters) { f->~IFilter(); MemoryHelper::free(f); }

		// Reparse the stored line.
		std::wstring command2 = outCommand.toStdWString();
		std::wstring params2 = outParams.toStdWString();
		std::vector<IFilter*> filters2 = factory.createFilter(L"", command2, params2);
		if (filters2.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: re-parse produced no filter (params='%ls')\n", c.name, params2.c_str());
			failures++;
			continue;
		}
		VSTPluginFilter* f1 = static_cast<VSTPluginFilter*>(filters2[0]);
		std::wstring chunk1 = f1->getChunkData();
		std::unordered_map<std::wstring, float> map1 = f1->getParamMap();
		for (IFilter* f : filters2) { f->~IFilter(); MemoryHelper::free(f); }

		bool ok = (chunk0 == chunk1) && (map0 == map1);
		if (!ok)
		{
			failures++;
			fprintf(stderr, "[VST selftest] %ls: LOSS. chunk %ls->%ls, params %zu->%zu\n",
				c.name, chunk0.c_str(), chunk1.c_str(), map0.size(), map1.size());
			for (auto& kv : map0)
			{
				auto it = map1.find(kv.first);
				if (it == map1.end())
					fprintf(stderr, "    dropped param '%ls'=%g\n", kv.first.c_str(), kv.second);
				else if (it->second != kv.second)
					fprintf(stderr, "    param '%ls' %g -> %g\n", kv.first.c_str(), kv.second, it->second);
			}
		}
		else
		{
			fprintf(stderr, "[VST selftest] %ls: OK (chunk len %zu, %zu params preserved)\n",
				c.name, chunk0.size(), map0.size());
		}
	}

	fprintf(stderr, "[VST selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

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

bool isCurrentProcessElevated()
{
	BOOL elevated = FALSE;
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	TOKEN_ELEVATION elevation;
	DWORD size = sizeof(elevation);
	if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
		elevated = elevation.TokenIsElevated;
	CloseHandle(token);
	return elevated == TRUE;
}

std::wstring widenArg(const char* arg)
{
	if (arg == nullptr)
		return std::wstring();
	int needed = MultiByteToWideChar(CP_UTF8, 0, arg, -1, nullptr, 0);
	if (needed <= 0)
		return std::wstring();
	std::wstring out(static_cast<size_t>(needed - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, arg, -1, out.data(), needed);
	return out;
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
// the child's exit code. Velopack runs hooks in the user's security context
// (the installer itself is unelevated for per-user installs), but the hook
// needs admin to write HKLM and register the APO DLL. We bridge the gap here.
int relaunchElevatedAndWait(int argc, char* argv[])
{
	wchar_t exePathBuffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
	if (length == 0)
	{
		fwprintf(stderr, L"GetModuleFileName failed (gle=%lu)\n", GetLastError());
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
		fwprintf(stderr, L"ShellExecuteEx(runas) failed (gle=%lu)\n", gle);
		// ERROR_CANCELLED (1223) means the user declined UAC.
		return gle == ERROR_CANCELLED ? 1223 : 1;
	}

	if (info.hProcess == nullptr)
		return 1;

	WaitForSingleObject(info.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(info.hProcess, &exitCode);
	CloseHandle(info.hProcess);
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

	if (!isCurrentProcessElevated())
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
			return rc == ApoRegistration::Result::Success ? 0 : static_cast<int>(rc);
		}
		if (matchesHook(arg, "--veloapp-updated"))
		{
			ApoRegistration::stopAudioService();
			auto rc = ApoRegistration::install(exeDir);
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
	// First thing in the process: field crashes (so far only reproducible on
	// foreign machines) must leave a minidump + breadcrumb report behind.
	CrashHandler::install();

	int hookResult = handleVelopackHook(argc, argv);
	if (hookResult >= 0)
		return hookResult;

	// Normal launch (not a Velopack hook; handleVelopackHook already handled and exited
	// for those). Initialise the Velopack runtime so UpdateManager resolves the correct
	// install context. Auto-apply-on-startup is off because we apply on exit instead.
	Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();

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
	fftw_make_planner_thread_safe();

	// Qt's plugins (platforms\qwindows.dll, imageformats, styles, tls) ship in a
	// "qt" subfolder beside the executable. addLibraryPath() resolves a relative
	// path against the current working directory, not the exe directory, so any
	// launch whose working directory is not the install folder (a file-type
	// association, a shortcut with a different "Start in", a debugger) left Qt
	// unable to locate its platform plugin and aborted with "This application
	// failed to start because no Qt platform plugin could be initialized".
	// Anchor the plugin search to the executable's own directory instead.
	{
		std::wstring pluginDir = executableDirectory();
		if (!pluginDir.empty())
		{
			pluginDir += L"\\qt";
			QCoreApplication::addLibraryPath(QString::fromStdWString(pluginDir));
		}
		else
		{
			QCoreApplication::addLibraryPath(QStringLiteral("qt"));
		}
	}

	// Font rendering: force Qt's FreeType font engine on Windows instead of the
	// default DirectWrite/GDI ClearType subpixel rasteriser. The bundled
	// Pretendard ships as CFF/OTF, which ClearType renders with subpixel colour
	// fringing that reads as blur on low-PPI monitors. High-DPI panels pack
	// enough pixels to hide it (the UI looks crisp at 4K/150%) but a 1080p
	// screen shows it plainly. FreeType uses grayscale antialiasing plus its own
	// CFF hinting, which stays consistent across monitors regardless of DPI.
	// Only set it when no platform is chosen externally, so the offscreen
	// gallery / CI (QT_QPA_PLATFORM=offscreen) still win and a user can opt back
	// into the default ClearType engine with QT_QPA_PLATFORM=windows.
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");

	// High-DPI: let Qt scale the whole UI by the monitor's device pixel ratio.
	// The editor used to disable Qt scaling (QT_ENABLE_HIGHDPI_SCALING=0) and
	// only hand-scale a few widget sizes through GUIHelper::scale, so on a 4K /
	// high-DPI display everything that went through QSS px/pt or was not scaled
	// by hand painted at physical pixels and looked tiny. Enable Qt scaling and
	// pin the logical DPI to 96 (AA_Use96Dpi) so GUIHelper::scale becomes a
	// no-op — Qt's device pixel ratio is then the single scaling source and we
	// avoid double scaling. PassThrough keeps fractional factors like 150%
	// exact instead of rounding them to 100%/200%.
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	QCoreApplication::setAttribute(Qt::AA_Use96Dpi);

	bool restart;
	do
	{
		QApplication application(argc, argv);
		application.setStyle(new CustomStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

		// Bundle the redesign's typefaces so the skins render identically
		// regardless of what is installed: DM Sans / DM Mono carry the Latin
		// look, Pretendard carries Korean. Static weight instances are used on
		// purpose — Qt does not reliably select a weight off a variable font's
		// wght axis, so a variable DM Sans / Pretendard rendered every QSS
		// font-weight (600/700) at the thin default. Registering Regular/Medium/
		// SemiBold/Bold per family lets font-weight resolve to a real face.
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-Regular.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-Medium.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-SemiBold.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans-Bold.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMMono-Regular.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMMono-Medium.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-Regular.otf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-Medium.otf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-SemiBold.otf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Pretendard-Bold.otf"));
		// Sarasa Mono K: a true fixed-width CJK face, subset to Hangul + ASCII.
		// It is the monospace Korean fallback so Korean in mono contexts keeps the
		// grid instead of dropping to the proportional Pretendard. Regular + Bold
		// cover the mono font-weights the skins use.
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SarasaMonoK-Regular.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SarasaMonoK-Bold.ttf"));

		// Fallback chain for painted (non-QSS) text where Qt resolves a single
		// QFont family. DM Sans/DM Mono lack Korean glyphs, so route CJK through
		// Pretendard -> Noto Sans -> Malgun Gothic (Korean) / Microsoft YaHei
		// (Chinese).
		const QStringList cjkChain = {
			QStringLiteral("Pretendard"),
			QStringLiteral("Noto Sans KR"), QStringLiteral("Noto Sans"),
			QStringLiteral("Malgun Gothic"), QStringLiteral("Microsoft YaHei")
		};
		QFont::insertSubstitutions(QStringLiteral("DM Sans"), cjkChain);
		// Mono text puts Sarasa Mono K ahead of the proportional CJK chain so
		// monospace Korean stays fixed-width; Consolas stays first for any Latin
		// the embedded DM Mono might lack.
		QFont::insertSubstitutions(QStringLiteral("DM Mono"),
			QStringList{ QStringLiteral("Consolas"), QStringLiteral("Sarasa Mono K") } + cjkChain);

		if (application.arguments().contains(QStringLiteral("--selftest-vst")))
			return runVstRoundTripSelfTest();

		// Headless screenshot gallery (skin program). Runs before the registry
		// skin/translator setup on purpose: the gallery applies each skin itself
		// and renders untranslated English strings for deterministic output.
		if (application.arguments().contains(QStringLiteral("--skin-gallery")))
			return SkinGallery::run(application.arguments());

		// Diagnostic self-test: crash deliberately so a field machine can verify
		// that the crash handler leaves a dump + report under
		// %LOCALAPPDATA%\EqualizerAPO-XT\crashdumps.
		if (application.arguments().contains(QStringLiteral("--selftest-crash")))
		{
			CrashHandler::setBreadcrumb(L"selftest-crash");
			volatile int* fault = nullptr;
			// cppcheck-suppress nullPointer ; the dereference is the whole point of the self-test
			*fault = 1; // intentional access violation
		}

		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		{
			QString skinId = settings.value(QStringLiteral("interface/skin"), QStringLiteral("studio")).toString();
			bool dark = settings.value(QStringLiteral("interface/dark"), GUIHelper::isDarkMode()).toBool();
			SkinManager::instance()->applySkin(skinId, dark);
			GUIHelper::applySkinPalette();
		}

		QVariant languageValue = settings.value("language");
		if (languageValue.isValid())
			QLocale::setDefault(QLocale(languageValue.toString()));
		else
			QLocale::setDefault(QLocale::system());

		QTranslator qtTranslator;
		if (qtTranslator.load(QLocale(), ":/translations/qtbase", "_"))
			application.installTranslator(&qtTranslator);

		QTranslator editorTranslator;
		if (editorTranslator.load(QLocale(), ":/translations/Editor", "_"))
			application.installTranslator(&editorTranslator);

		QString configPath = QDir::currentPath();
		if (RegistryHelper::keyExists(APP_REGPATH) && RegistryHelper::valueExists(APP_REGPATH, L"ConfigPath"))
			configPath = QString::fromStdWString(RegistryHelper::readValue(APP_REGPATH, L"ConfigPath"));
		QDir configDir(configPath);

		if (!RegistryHelper::keyExists(USER_REGPATH))
			RegistryHelper::createKey(USER_REGPATH);

		if (!RegistryHelper::keyExists(EDITOR_REGPATH))
			RegistryHelper::createKey(EDITOR_REGPATH);

		if (!RegistryHelper::keyExists(EDITOR_PER_FILE_REGPATH))
			RegistryHelper::createKey(EDITOR_PER_FILE_REGPATH);

		MainWindow w(configDir);
		w.show();

		QCommandLineParser parser;
		parser.process(application);
		QStringList args = parser.positionalArguments();
		if (args.isEmpty() && w.isEmpty())
			args = QStringList("config.txt");

		for (const QString& arg : args)
			w.load(configDir.absoluteFilePath(arg));

		bool firstRun = VelopackBootstrap::isFirstRun();
		if (firstRun)
			launchDeviceSelector(executableDirectory());
		else
			w.doChecks();

		if (VelopackBootstrap::isVelopackInstall() && !firstRun)
		{
			// Defer the background download so it does not race with audio service
			// work or a Device Selector launch right after the Editor opens.
			// 60s is long enough that the initial GUI paint, config load, and
			// device enumeration are all comfortably finished. The download runs on
			// its own worker thread and just stages the update for apply-on-exit.
			QTimer::singleShot(60000, qApp, []() {
				std::string channel;
#ifdef EAPO_UPDATE_CHANNEL
				channel = EAPO_UPDATE_CHANNEL;
#endif
				VelopackBootstrap::startBackgroundDownload("https://github.com/115dkk/EqualizerAPO-XT", channel);
			});
		}

		result = application.exec();

		restart = w.shouldRestart();
	}
	while (restart);

	// If the background worker staged an update, apply it now. exec() has returned and
	// the QApplication is destroyed, so no other thread is writing to the install dir.
	// The apply is silent and does not restart; the new version comes up next launch.
	if (VelopackBootstrap::isVelopackInstall() && VelopackBootstrap::hasPendingUpdate())
		VelopackBootstrap::applyPendingUpdateAndExit();

	return result;
}
