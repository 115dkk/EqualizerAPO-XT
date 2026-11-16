/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    EqualizerAPO-XT-Setup.exe - the auto-detect front-door installer.

    It detects the machine's native CPU architecture and best supported x86
    instruction set, then downloads and runs the matching per-variant Velopack
    Setup.exe from the latest GitHub release. The user chooses nothing.

    Design and rationale: docs/AutoDetectInstaller.md. The short version:
      - Built as a 32-bit (x86) Win32 GUI app so one binary runs on x64 natively
        and on ARM64 under emulation. CPUID/XGETBV report true CPU/OS state
        regardless of process bitness, and IsWow64Process2 reports the native
        machine even under emulation, so detection is accurate from x86.
      - It does NOT touch the six per-channel Velopack packages or their
        per-channel auto-update path; it only picks which one to install.
      - The downloaded Setup.exe is verified against the SHA256SUMS.txt asset
        that CI publishes to the same release: the SHA-256 of the file must
        match the asset's line before anything is launched. If the checksums
        file cannot be downloaded, does not list the asset, or the hash
        differs, the download is deleted and the process exits with code 4.

    The channel strings live in one table in AutoInstallerLogic.cpp; this
    binary cannot read .github/simd-variants.psd1, so
    .github/scripts/Test-VariantSync.ps1 holds the two together.
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>      // BCrypt* SHA-256 hashing (CNG)
#include <shellapi.h>    // CommandLineToArgvW
#include <intrin.h>      // __cpuid, __cpuidex, _xgetbv
#include <cstdlib>       // _wcstoui64 (Content-Length)
#include <functional>
#include <string>
#include <thread>

#include "../platform/windows/ComPtr.h"
#include "../release/ReleaseAssetNames.h"
#include "../platform/windows/Win32Resource.h"
#include "../platform/windows/ConsoleOutput.h"
#include "../version.h"
#include "AutoInstallerLogic.h"
#include "InstallerUiModel.h"
#include "InstallerWindow.h"

// The decision logic (channel mapping, asset grammar, checksum parsing, flag
// scan) lives in AutoInstallerLogic so EditorLogicTests can compile it; this
// TU keeps the Win32 machinery and the entry point.
using namespace AutoInstallerLogic;

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
struct BCryptAlgorithmTraits
{
    using resource_type = BCRYPT_ALG_HANDLE;
    static resource_type invalid() noexcept { return nullptr; }
    static bool isValid(resource_type value) noexcept { return value != nullptr; }
    static void close(resource_type value) noexcept { BCryptCloseAlgorithmProvider(value, 0); }
};

struct BCryptHashTraits
{
    using resource_type = BCRYPT_HASH_HANDLE;
    static resource_type invalid() noexcept { return nullptr; }
    static bool isValid(resource_type value) noexcept { return value != nullptr; }
    static void close(resource_type value) noexcept { BCryptDestroyHash(value); }
};

using UniqueBCryptAlgorithm = winutil::UniqueResource<BCryptAlgorithmTraits>;
using UniqueBCryptHash = winutil::UniqueResource<BCryptHashTraits>;

// GitHub repository that hosts the releases. Matches the GithubSource URL used by
// the in-app updater (Editor/main.cpp).
const wchar_t* kReleasesPage = EAPO_REPO_URL_W L"/releases/latest";
const wchar_t* kUserAgent = L"EqualizerAPO-XT-Setup";

// IsWow64Process2 reports the native machine even when this x86 process runs
// under x64/ARM64 emulation. Fall back to GetNativeSystemInfo on the (very old)
// systems that lack it.
bool isArm64Native()
{
    typedef BOOL(WINAPI * PFN_IsWow64Process2)(HANDLE, USHORT*, USHORT*);
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 != nullptr)
    {
        PFN_IsWow64Process2 fn =
            reinterpret_cast<PFN_IsWow64Process2>(GetProcAddress(kernel32, "IsWow64Process2"));
        if (fn != nullptr)
        {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (fn(GetCurrentProcess(), &processMachine, &nativeMachine))
                return nativeMachine == IMAGE_FILE_MACHINE_ARM64;
        }
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
}

// Gather the CPU/OS facts the channel choice needs. The feature bits are
// gated on the OS having actually enabled the wider register state
// (XGETBV/XCR0), so we never pick a build the OS cannot context-switch.
CpuFeatures detectCpuFeatures()
{
    CpuFeatures features;
    if (isArm64Native())
    {
        features.arm64Native = true;
        return features;
    }

    int info[4] = { 0, 0, 0, 0 };
    __cpuid(info, 0);
    const int maxLeaf = info[0];

    __cpuid(info, 1);
    const bool osxsave = (info[2] & (1 << 27)) != 0;   // ECX[27]
    const bool cpuAvx = (info[2] & (1 << 28)) != 0;    // ECX[28]

    bool osYmm = false;   // XMM + YMM state enabled by the OS
    bool osZmm = false;   // + opmask + ZMM_Hi256 + Hi16_ZMM
    if (osxsave)
    {
        const unsigned long long xcr0 = _xgetbv(0); // _XCR_XFEATURE_ENABLED_MASK
        osYmm = (xcr0 & 0x6) == 0x6;                 // bits 1,2
        osZmm = osYmm && ((xcr0 & 0xE0) == 0xE0);    // bits 5,6,7
    }
    features.avx = cpuAvx && osYmm;

    if (maxLeaf >= 7)
    {
        __cpuidex(info, 7, 0);
        features.avx2 = ((info[1] & (1 << 5)) != 0) && features.avx; // EBX[5], needs YMM
        features.avx512f = ((info[1] & (1 << 16)) != 0) && osZmm;    // EBX[16], needs ZMM

        __cpuidex(info, 7, 1);
        const bool avx10Enumerated = (info[3] & (1 << 19)) != 0;     // EDX[19]
        if (avx10Enumerated && maxLeaf >= 0x24)
        {
            __cpuidex(info, 0x24, 0);
            const int avx10Version = info[1] & 0xFF;                 // EBX[7:0]
            const bool avx10Has512 = (info[1] & (1 << 18)) != 0;     // EBX[18] AVX10/512
            features.avx10_1 = (avx10Version >= 1) && avx10Has512 && osZmm;
        }
    }
    return features;
}

// Resolve the best build channel for this machine.
std::wstring detectChannel(int* outIndex)
{
    return channelForCpu(detectCpuFeatures(), outIndex);
}

// Download an HTTPS GitHub URL into a caller-supplied byte sink. WinHTTP follows
// GitHub's redirect to the objects CDN automatically (https->https, allowed by
// the default redirect policy). progress, when set, is called per received
// chunk with (bytesSoFar, totalBytes); totalBytes is 0 without Content-Length.
DownloadResult download(const std::wstring& url, const ByteSink& sink,
    const DownloadProgress& progress)
{
    DownloadResult result;
    std::wstring& error = result.error;
    const std::wstring prefix = L"https://github.com";
    if (!url.starts_with(prefix + L"/"))
    {
        error = L"Could not create the download request.";
        return result;
    }
    const std::wstring path = url.substr(prefix.size());
    winutil::UniqueWinHttpHandle session;
    winutil::UniqueWinHttpHandle connect;
    winutil::UniqueWinHttpHandle request;
    unsigned long long totalBytes = 0;
    unsigned long long receivedBytes = 0;

    session.reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        error = L"Could not initialise WinHTTP.";
        goto cleanup;
    }

    // Bound every blocking phase, so a stalled connection ends in an error
    // the window can show instead of a system default (potentially
    // multi-minute) wait.
    WinHttpSetTimeouts(session.get(), 15000, 15000, 30000, 30000);

    connect.reset(WinHttpConnect(session.get(), L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect)
    {
        error = L"Could not connect to github.com.";
        goto cleanup;
    }

    request.reset(WinHttpOpenRequest(connect.get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request)
    {
        error = L"Could not create the download request.";
        goto cleanup;
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
    {
        error = L"No response from the download server. Check your internet connection.";
        goto cleanup;
    }

    {
        DWORD size = sizeof(result.statusCode);
        if (!WinHttpQueryHeaders(request.get(),
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &result.statusCode, &size, WINHTTP_NO_HEADER_INDEX))
        {
            error = L"No response from the download server. Check your internet connection.";
            goto cleanup;
        }
        if (result.statusCode != 200)
            goto cleanup;
    }

    {
        wchar_t lengthText[32] = {};
        DWORD lengthSize = sizeof(lengthText);
        if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH,
                WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX))
        {
            totalBytes = _wcstoui64(lengthText, nullptr, 10);
        }
    }

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            error = L"The download was interrupted.";
            goto cleanup;
        }
        if (available == 0)
            break;

        std::string buffer;
        // Do not let an untrusted available-byte count allocate an entire
        // installer in this baseline Win32 process.
        const DWORD chunkSize = available < 64 * 1024 ? available : 64 * 1024;
        buffer.resize(chunkSize);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), &buffer[0], chunkSize, &read) || read == 0)
        {
            error = L"The download was interrupted.";
            goto cleanup;
        }

        if (!sink(std::string_view(buffer.data(), read), error))
            goto cleanup;

        receivedBytes += read;
        if (progress)
            progress(receivedBytes, totalBytes);
    }

    if (progress)
        progress(receivedBytes, totalBytes);

cleanup:
    return result;
}

// Compute the SHA-256 of a file as lowercase hex using CNG. The file is
// streamed in 64 KiB chunks so the installer never has to fit in memory.
// CNG returns NTSTATUS where STATUS_SUCCESS is 0, so any nonzero status is
// treated as a failure.
bool sha256OfFile(HANDLE file, std::wstring& outHexLower, std::wstring& error)
{
    bool ok = false;
    UniqueBCryptAlgorithm algorithm;
    UniqueBCryptHash hash;
    UCHAR digest[32] = {};
    std::string buffer;
    buffer.resize(64 * 1024);

    if (BCryptOpenAlgorithmProvider(algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    {
        error = L"Could not initialise the SHA-256 provider.";
        goto cleanup;
    }
    if (BCryptCreateHash(algorithm.get(), hash.put(), nullptr, 0, nullptr, 0, 0) != 0)
    {
        error = L"Could not create a SHA-256 hash object.";
        goto cleanup;
    }

    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(file, &buffer[0], static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            error = L"Could not read the downloaded installer for verification.";
            goto cleanup;
        }
        if (read == 0)
            break;
        if (BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(&buffer[0]), read, 0) != 0)
        {
            error = L"Could not hash the downloaded installer.";
            goto cleanup;
        }
    }

    if (BCryptFinishHash(hash.get(), digest, sizeof(digest), 0) != 0)
    {
        error = L"Could not finish hashing the downloaded installer.";
        goto cleanup;
    }

    {
        const wchar_t* hexDigits = L"0123456789abcdef";
        outHexLower.clear();
        outHexLower.reserve(sizeof(digest) * 2);
        for (size_t i = 0; i < sizeof(digest); ++i)
        {
            outHexLower += hexDigits[digest[i] >> 4];
            outHexLower += hexDigits[digest[i] & 0xF];
        }
    }

    ok = true;

cleanup:
    return ok;
}

// Verification and checksum parsing live in the injected install flow. Only
// the OS-backed hash and file lifetime remain here.

// Launch the downloaded per-variant Setup.exe. When silent, forward Velopack's
// -s/--silent and wait so a caller knows when the install finished; otherwise
// hand off to Velopack's own install UI and return immediately.
bool launchSetup(const std::wstring& setupPath, bool silent, DWORD& exitCode)
{
    std::wstring commandLine = L"\"" + setupPath + L"\"";
    if (silent)
        commandLine += L" --silent";

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    winutil::UniqueProcessInformation proc;

    // CreateProcessW may modify the command-line buffer, so pass a writable copy.
    std::wstring mutableCommand = commandLine;
    if (!CreateProcessW(setupPath.c_str(), &mutableCommand[0], nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &startup, proc.put()))
    {
        return false;
    }

    if (silent)
    {
        WaitForSingleObject(proc.process(), INFINITE);
        // Audit #250 F045: an unchecked query left exitCode uninitialized
        // garbage, letting silent mode report success for a failed setup.
        if (!GetExitCodeProcess(proc.process(), &exitCode))
            return false;
    }
    else
    {
        exitCode = 0;
    }

    return true;
}

// hasFlag / flagValue moved to AutoInstallerLogic.

void writeTextFile(const wchar_t* path, const std::wstring& text)
{
    winutil::UniqueHandle file(CreateFileW(path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return;
    std::string utf8;
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1)
    {
        utf8.resize(needed - 1);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], needed - 1, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

// Mark the verified download as coming from the internet - the same
// Zone.Identifier stream a browser writes. Defender's behavior heuristics
// read launching an untagged, freshly downloaded executable as dropper
// behavior, and the tag lets SmartScreen judge the child on its own record
// instead of silently inheriting this process's. Best effort: alternate
// streams do not exist on FAT volumes.
void tagAsInternetDownload(const std::wstring& path, const std::wstring& sourceUrl)
{
    const std::wstring streamPath = path + L":Zone.Identifier";
    const std::wstring text = std::wstring(L"[ZoneTransfer]\r\nZoneId=3\r\nReferrerUrl=") +
        kReleasesPage + L"\r\nHostUrl=" + sourceUrl + L"\r\n";
    writeTextFile(streamPath.c_str(), text);
}

class DownloadedSetup final : public HeldSetup
{
public:
    ~DownloadedSetup() override
    {
        file.reset();
        if (!launched && !path.empty())
            DeleteFileW(path.c_str());
        directoryHandle.reset();
        if (!launched && !directory.empty())
            RemoveDirectoryW(directory.c_str());
    }

    static std::unique_ptr<HeldSetup> create(const std::wstring& name, std::wstring& error)
    {
        auto setup = std::make_unique<DownloadedSetup>();
        // Audit #250 F045: falling back to the bare file name meant downloading
        // and executing an installer from the current directory. No temp path,
        // no download.
        wchar_t temp[MAX_PATH] = {};
        const DWORD length = GetTempPathW(MAX_PATH, temp);
        if (length == 0 || length >= MAX_PATH)
        {
            error = L"No temporary directory is available for the download.";
            return nullptr;
        }
        // An unpredictable, fresh directory per run; never reuse a name
        // planted by another process (audit #467 TD-56).
        UCHAR random[16] = {};
        if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        {
            error = L"Could not create a temporary file for the download.";
            return nullptr;
        }
        std::wstring directory = std::wstring(temp) + L"EqualizerAPO-XT-";
        for (UCHAR byte : random)
        {
            directory += L"0123456789abcdef"[byte >> 4];
            directory += L"0123456789abcdef"[byte & 0xf];
        }
        if (!CreateDirectoryW(directory.c_str(), nullptr))
        {
            error = L"Could not create a temporary file for the download.";
            return nullptr;
        }
        setup->directory = directory;
        // Pin the containing directory too: pinning only the file would still
        // permit an ancestor rename followed by pathname substitution.
        setup->directoryHandle.reset(CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info = {};
        if (!setup->directoryHandle || !GetFileInformationByHandle(setup->directoryHandle.get(), &info)
            || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            error = L"Could not create a temporary file for the download.";
            return nullptr;
        }
        setup->path = directory + L"\\" + name;
        setup->file.reset(CreateFileW(setup->path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!setup->file)
        {
            // Do not delete a file we did not create.
            setup->path.clear();
            error = L"Could not create a temporary file for the download.";
            return nullptr;
        }
        return setup;
    }

    bool write(std::string_view bytes, std::wstring& error) override
    {
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
            || written != bytes.size())
        {
            error = L"Could not write the downloaded installer to disk.";
            return false;
        }
        return true;
    }

    bool sealAndHash(std::wstring& hash, std::wstring& error) override
    {
        // Drop write access before opening the read-only lease. A replacement
        // during this transition is hashed too and rejected unless it matches.
        file.reset();
        file.reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info = {};
        if (!file || !GetFileInformationByHandle(file.get(), &info)
            || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
        {
            error = L"Could not open the downloaded installer for verification.";
            return false;
        }
        // FILE_SHARE_READ permits the loader's read/execute open, but not a
        // writer or delete/rename. Keep this handle through CreateProcessW
        // (audit #467 TD-56; CreateFileW / ZwCreateFile sharing contract).
        return sha256OfFile(file.get(), hash, error);
    }

    LaunchResult launch(bool silent, const std::wstring& sourceUrl)
    {
        // Only a verified file is tagged and launched. Alternate streams have
        // independent read/write sharing; the held main stream stays immutable.
        tagAsInternetDownload(path, sourceUrl);
        LaunchResult result;
        result.started = launchSetup(path, silent, result.exitCode);
        launched = result.started;
        // The asynchronous Velopack bootstrap may reopen its own image after
        // CreateProcess returns. Preserve successful downloads as before.
        return result;
    }

private:
    std::wstring directory;
    std::wstring path;
    winutil::UniqueHandle directoryHandle;
    winutil::UniqueHandle file;
    bool launched = false;
};

// Print the detection result for --detect-only. Try the parent console first
// (so it works from a shell), then fall back to a message box and an optional
// --out file. Returns the channel index for use as the process exit code.
int reportDetection(const std::wstring& channel, const std::wstring& url,
    const wchar_t* outPath, int index)
{
    const std::wstring text = detectionOutput(channel, url);

    winutil::ParentConsole console;
    if (console)
    {
        console.write(text);
    }
    else
    {
        MessageBoxW(nullptr, (channel + L"\n" + url).c_str(),
            L"EqualizerAPO-XT - detected variant", MB_OK | MB_ICONINFORMATION);
    }

    if (outPath != nullptr)
        writeTextFile(outPath, text);

    return index;
}

// Runs detect -> download -> verify -> launch, reporting into the window
// when one is attached. ui == nullptr with silent == false is the fallback
// for a failed window creation: the flow then reports errors the pre-window
// way, through message boxes. Silent mode is fully headless - errors only
// reach the exit code, so an unattended install can never block on a dialog.
int runInstallFlow(InstallerUi::InstallerWindow* ui, bool silent)
{
    using namespace InstallerUi;
    const bool useMessageBoxes = (ui == nullptr && !silent);

    const auto fail = [ui, useMessageBoxes](int step, int exitCode, const std::wstring& text)
    {
        if (ui != nullptr)
        {
            ui->update([step, text](Model& model) { failStep(model, step, text); });
            ui->finish(0);
        }
        else if (useMessageBoxes)
        {
            const std::wstring message = text +
                L"\n\nYou can download a build manually from:\n" + kReleasesPage;
            MessageBoxW(nullptr, message.c_str(),
                L"EqualizerAPO-XT - install failed", MB_OK | MB_ICONERROR);
        }
        return exitCode;
    };

    unsigned long long downloadedTotal = 0;
    unsigned long long lastPostedBytes = 0;
    InstallOperations operations;
    operations.transport = download;
    operations.createFile = DownloadedSetup::create;
    std::wstring sourceUrl;
    operations.launch = [&sourceUrl](HeldSetup& file, bool quiet)
    {
        return static_cast<DownloadedSetup&>(file).launch(quiet, sourceUrl);
    };
    operations.progress = [ui, &downloadedTotal, &lastPostedBytes](
        unsigned long long received, unsigned long long total)
    {
        downloadedTotal = received;
        if (ui == nullptr)
            return;
        // Post at most every 512 KiB (plus the final chunk) so a fast
        // connection cannot flood the message queue.
        if (received - lastPostedBytes < 512 * 1024 && !(total != 0 && received >= total))
            return;
        lastPostedBytes = received;
        ui->update([received, total](Model& model)
        {
            model.downloadedBytes = received;
            model.totalBytes = total;
            model.details[kStepDownload] = formatDownloadDetail(received, total);
        });
    };

    operations.stageChanged = [ui, &downloadedTotal, &sourceUrl](InstallStage stage, const std::wstring& detail)
    {
        if (stage == InstallStage::Download)
            sourceUrl = downloadUrl(detail);
        if (ui == nullptr)
            return;
        const unsigned long long total = downloadedTotal;
        ui->update([stage, detail, total](Model& model)
        {
            switch (stage)
            {
            case InstallStage::Detect:
                startStep(model, kStepDetect, L"Reading CPUID and the OS-enabled register state");
                break;
            case InstallStage::Download:
                finishStep(model, kStepDetect, describeChannel(detail) + L" \x2014 " + detail + L" build");
                startStep(model, kStepDownload, L"Connecting to github.com");
                break;
            case InstallStage::Verify:
                finishStep(model, kStepDownload, formatByteSize(total) + L" downloaded");
                startStep(model, kStepVerify, L"Computing SHA-256 and matching the release checksums");
                break;
            case InstallStage::Launch:
                finishStep(model, kStepVerify,
                    L"SHA-256 matches the published checksum (" + shortHash(detail) + L")");
                startStep(model, kStepLaunch, L"Handing off to the Velopack installer");
                break;
            case InstallStage::Complete:
                break;
            }
        });
    };

    const InstallResult result = install(detectCpuFeatures(), silent, operations);
    if (result.stage == InstallStage::Download)
        return fail(kStepDownload, kExitDownloadFailed, result.error);
    if (result.stage == InstallStage::Verify)
        return fail(kStepVerify, kExitVerifyFailed, result.error);
    if (result.stage == InstallStage::Launch)
        return fail(kStepLaunch, kExitLaunchFailed, result.error);
    if (silent)
        return static_cast<int>(result.exitCode);

    if (ui != nullptr)
    {
        ui->update([](Model& model)
        {
            finishStep(model, kStepLaunch, L"Velopack installer started \u2014 this window closes itself");
            model.completed = true;
        });
        ui->finish(1500);
    }
    return kExitSuccess;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    winutil::UniqueLocalPtr<wchar_t*> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    const bool detectOnly = argv && hasFlag(argc, argv.get(), L"--detect-only");
    const bool silent = argv && hasFlag(argc, argv.get(), L"--silent");
    const wchar_t* outPath = argv ? flagValue(argc, argv.get(), L"--out") : nullptr;
    const wchar_t* uiShotDir = argv ? flagValue(argc, argv.get(), L"--ui-shot") : nullptr;

    if (detectOnly)
    {
        int index = kAvx2;
        const std::wstring channel = detectChannel(&index);
        return reportDetection(channel, downloadUrl(channel), outPath, index);
    }

    // COM is for WIC (--ui-shot) and shell handoffs; the install flow itself
    // no longer needs it.
    winutil::ComApartment apartment(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Renders the window states as PNGs - the review evidence for UI changes.
    if (uiShotDir != nullptr)
        return InstallerUi::InstallerWindow::renderShots(uiShotDir) ? 0 : 1;

    if (silent)
        return runInstallFlow(nullptr, true);

    InstallerUi::InstallerWindow window;
    if (!window.create(instance))
        return runInstallFlow(nullptr, false);

    // The flow's own result is the exit code: closing the window does not
    // end or cancel it, so the loop always outlives the worker's finish().
    int result = InstallerUi::kExitSuccess;
    std::thread worker([&window, &result] { result = runInstallFlow(&window, false); });
    InstallerUi::InstallerWindow::runMessageLoop();
    worker.join();
    return result;
}
