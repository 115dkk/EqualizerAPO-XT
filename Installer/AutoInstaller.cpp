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

    The six channel strings below MUST stay in sync with
    .github/simd-variants.psd1, .github/workflows/build.yml and
    .github/scripts/New-ReleaseNotes.ps1 (this file is compiled C++ and cannot
    read the manifest).
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

// Checksums asset that CI publishes to every release, one sha256sum-style
// "<lowercase-hex-sha256>  <name>" line per asset. The grammar header keeps
// this in step with the upload in .github/workflows/build.yml.
const wchar_t* kChecksumsAssetName = ReleaseAssetNames::checksumsAssetName;

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

std::wstring tempFilePath(const std::wstring& fileName)
{
    // Audit #250 F045: falling back to the bare file name meant downloading
    // and executing an installer from the current directory. No temp path,
    // no download.
    wchar_t dir[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, dir);
    if (len == 0 || len > MAX_PATH)
        return std::wstring();
    return std::wstring(dir) + fileName;
}

enum class DownloadOutcome
{
    Ok,
    Failed,
    Canceled
};

// Download github.com<path> to outFile over HTTPS. WinHTTP follows GitHub's
// redirect to the objects CDN automatically (https->https, allowed by the
// default redirect policy). progress, when set, is called per received chunk
// with (bytesSoFar, totalBytes) - totalBytes is 0 when the server sent no
// Content-Length - and cancels the download by returning false. Returns Ok
// on HTTP 200 + complete write.
DownloadOutcome downloadToFile(const std::wstring& path, const std::wstring& outFile,
    std::wstring& error,
    const std::function<bool(unsigned long long, unsigned long long)>& progress = {})
{
    DownloadOutcome outcome = DownloadOutcome::Failed;
    winutil::UniqueWinHttpHandle session;
    winutil::UniqueWinHttpHandle connect;
    winutil::UniqueWinHttpHandle request;
    winutil::UniqueHandle file;
    unsigned long long totalBytes = 0;
    unsigned long long receivedBytes = 0;

    session.reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        error = L"Could not initialise WinHTTP.";
        goto cleanup;
    }

    // Bound every blocking phase: with the window's close button acting as a
    // cancel, the worker must never sit in a system default (potentially
    // multi-minute) wait after the user already gave up.
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
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        if (statusCode != 200)
        {
            error = L"The matching installer was not found on the release page (HTTP " +
                std::to_wstring(statusCode) + L").";
            goto cleanup;
        }
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

    file.reset(CreateFileW(outFile.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
    {
        error = L"Could not create a temporary file for the download.";
        goto cleanup;
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
        buffer.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), &buffer[0], available, &read) || read == 0)
        {
            error = L"The download was interrupted.";
            goto cleanup;
        }

        DWORD written = 0;
        if (!WriteFile(file.get(), buffer.data(), read, &written, nullptr) || written != read)
        {
            error = L"Could not write the downloaded installer to disk.";
            goto cleanup;
        }

        receivedBytes += read;
        if (progress && !progress(receivedBytes, totalBytes))
        {
            outcome = DownloadOutcome::Canceled;
            goto cleanup;
        }
    }

    if (progress)
        progress(receivedBytes, totalBytes);
    outcome = DownloadOutcome::Ok;

cleanup:
    {
        const bool removePartialFile = outcome != DownloadOutcome::Ok && static_cast<bool>(file);
        file.reset();
        if (removePartialFile)
            DeleteFileW(outFile.c_str());
    }
    return outcome;
}

// Compute the SHA-256 of a file as lowercase hex using CNG. The file is
// streamed in 64 KiB chunks so the installer never has to fit in memory.
// CNG returns NTSTATUS where STATUS_SUCCESS is 0, so any nonzero status is
// treated as a failure.
bool sha256OfFile(const std::wstring& path, std::wstring& outHexLower, std::wstring& error)
{
    bool ok = false;
    UniqueBCryptAlgorithm algorithm;
    UniqueBCryptHash hash;
    winutil::UniqueHandle file;
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

    file.reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
    {
        error = L"Could not open the downloaded installer for verification.";
        goto cleanup;
    }

    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(file.get(), &buffer[0], static_cast<DWORD>(buffer.size()), &read, nullptr))
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

// Read a small file fully into memory. The checksums list is at most a few
// kilobytes; refuse anything over 1 MiB so an unexpected response cannot
// balloon.
bool readSmallFile(const std::wstring& path, std::string& outData)
{
    bool ok = false;
    winutil::UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file.get(), &size) && size.QuadPart > 0 && size.QuadPart <= 1024 * 1024)
    {
        outData.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file.get(), &outData[0], static_cast<DWORD>(outData.size()), &read, nullptr) &&
            read == outData.size();
    }

    if (!ok)
        outData.clear();
    return ok;
}

// Verify the downloaded installer against the SHA256SUMS.txt asset that CI
// publishes to the same release. Returns true only when the checksums file
// downloads, lists the installer, and the SHA-256 matches. outActualHash,
// when set, receives the computed digest so the window can show it.
bool verifySetupChecksum(const std::wstring& setupFile, const std::wstring& setupName,
    std::wstring& error, std::wstring* outActualHash = nullptr)
{
    const std::wstring sumsFile = tempFilePath(kChecksumsAssetName);

    std::wstring downloadError;
    if (downloadToFile(latestAssetPath(kChecksumsAssetName), sumsFile, downloadError)
        != DownloadOutcome::Ok)
    {
        error = L"The integrity checksums file could not be downloaded from the release page."
            L" If a release was published only moments ago it may still be uploading;"
            L" please try again in a few minutes.";
        return false;
    }

    std::string text;
    const bool readOk = readSmallFile(sumsFile, text);
    DeleteFileW(sumsFile.c_str());
    if (!readOk)
    {
        error = L"The integrity checksums file could not be read.";
        return false;
    }

    const std::wstring expected = expectedHashFromChecksums(text, setupName);
    if (expected.empty())
    {
        error = L"The release's checksums file does not list the downloaded installer.";
        return false;
    }

    std::wstring actual;
    if (!sha256OfFile(setupFile, actual, error))
        return false;
    if (outActualHash != nullptr)
        *outActualHash = actual;

    if (actual != expected)
    {
        error = L"The downloaded installer failed its integrity check.";
        return false;
    }
    return true;
}

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

// Print the detection result for --detect-only. Try the parent console first
// (so it works from a shell), then fall back to a message box and an optional
// --out file. Returns the channel index for use as the process exit code.
int reportDetection(const std::wstring& channel, const std::wstring& url,
    const wchar_t* outPath, int index)
{
    const std::wstring text = channel + L"\n" + url + L"\n";

    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        winutil::UniqueHandle conout(CreateFileW(L"CONOUT$", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (conout)
        {
            DWORD written = 0;
            WriteConsoleW(conout.get(), text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
        }
        FreeConsole();
    }
    else
    {
        MessageBoxW(nullptr, (channel + L"\n" + url).c_str(),
            L"EqualizerAPO-XT - detected variant", MB_OK | MB_ICONINFORMATION);
    }

    if (outPath != nullptr)
        writeTextFile(outPath, channel + L"\n" + url + L"\n");

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
            ui->finish(exitCode, 0);
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

    if (ui != nullptr)
    {
        ui->update([](Model& model)
        {
            startStep(model, kStepDetect, L"Reading CPUID and the OS-enabled register state");
        });
    }
    int index = kAvx2;
    const std::wstring channel = detectChannel(&index);
    if (ui != nullptr)
    {
        const std::wstring detected = describeChannel(channel) + L" \u2014 " + channel + L" build";
        ui->update([detected](Model& model) { finishStep(model, kStepDetect, detected); });
    }

    const std::wstring setupName = assetName(channel);
    const std::wstring outFile = tempFilePath(setupName);
    if (outFile.empty())
        return fail(kStepDownload, kExitDownloadFailed,
            L"No temporary directory is available for the download.");

    if (ui != nullptr)
        ui->update([](Model& model) { startStep(model, kStepDownload, L"Connecting to github.com"); });

    unsigned long long downloadedTotal = 0;
    unsigned long long lastPostedBytes = 0;
    const auto progress = [ui, &downloadedTotal, &lastPostedBytes](
        unsigned long long received, unsigned long long total)
    {
        downloadedTotal = received;
        if (ui == nullptr)
            return true;
        if (ui->isCancelRequested())
            return false;
        // Post at most every 512 KiB (plus the final chunk) so a fast
        // connection cannot flood the message queue.
        if (received - lastPostedBytes < 512 * 1024 && !(total != 0 && received >= total))
            return true;
        lastPostedBytes = received;
        ui->update([received, total](Model& model)
        {
            model.downloadedBytes = received;
            model.totalBytes = total;
            model.details[kStepDownload] = formatDownloadDetail(received, total);
        });
        return true;
    };

    std::wstring error;
    const DownloadOutcome downloaded = downloadToFile(assetPath(channel), outFile, error, progress);
    if (downloaded == DownloadOutcome::Canceled)
    {
        if (ui != nullptr)
            ui->finish(kExitCanceled, 0);
        return kExitCanceled;
    }
    if (downloaded != DownloadOutcome::Ok)
        return fail(kStepDownload, kExitDownloadFailed, error);

    if (ui != nullptr)
    {
        const std::wstring downloadedText = formatByteSize(downloadedTotal) + L" downloaded";
        ui->update([downloadedText](Model& model)
        {
            finishStep(model, kStepDownload, downloadedText);
            startStep(model, kStepVerify, L"Computing SHA-256 and matching the release checksums");
        });
    }

    // Check the download against the release's SHA256SUMS.txt before anything
    // is executed. A failed or impossible verification discards the download.
    std::wstring actualHash;
    if (!verifySetupChecksum(outFile, setupName, error, &actualHash))
    {
        DeleteFileW(outFile.c_str());
        return fail(kStepVerify, kExitVerifyFailed, error);
    }
    if (ui != nullptr)
    {
        const std::wstring verifiedText =
            L"SHA-256 matches the published checksum (" + shortHash(actualHash) + L")";
        ui->update([verifiedText](Model& model) { finishStep(model, kStepVerify, verifiedText); });
    }

    // Only a verified file is tagged and launched.
    tagAsInternetDownload(outFile, downloadUrl(channel));

    if (ui != nullptr)
        ui->update([](Model& model) { startStep(model, kStepLaunch, L"Handing off to the Velopack installer"); });
    DWORD setupExit = 0;
    if (!launchSetup(outFile, silent, setupExit))
        return fail(kStepLaunch, kExitLaunchFailed,
            L"The downloaded installer could not be started.");
    if (silent)
        return static_cast<int>(setupExit);

    if (ui != nullptr)
    {
        ui->update([](Model& model)
        {
            finishStep(model, kStepLaunch, L"Velopack installer started \u2014 this window closes itself");
            model.completed = true;
        });
        ui->finish(kExitSuccess, 1500);
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

    std::thread worker([&window] { runInstallFlow(&window, false); });
    const int result = window.runMessageLoop();
    worker.join();
    return result;
}
