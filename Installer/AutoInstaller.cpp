/*
    This file is part of EqualizerAPO-XT.

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
#include <shlobj.h>      // IProgressDialog
#include <shellapi.h>    // CommandLineToArgvW
#include <intrin.h>      // __cpuid, __cpuidex, _xgetbv
#include <string>

#include "../version.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
// GitHub repository that hosts the releases. Matches the GithubSource URL used by
// the in-app updater (Editor/main.cpp).
const wchar_t* kRepoOwner = L"115dkk";
const wchar_t* kRepoName = L"EqualizerAPO-XT";
const wchar_t* kReleasesPage = EAPO_REPO_URL_W L"/releases/latest";
const wchar_t* kUserAgent = L"EqualizerAPO-XT-Setup";

// Checksums asset that CI publishes to every release, one sha256sum-style
// "<lowercase-hex-sha256>  <name>" line per asset. The name must match the
// upload in .github/workflows/build.yml.
const wchar_t* kChecksumsAssetName = L"SHA256SUMS.txt";

// Channel index used as the process exit code for --detect-only, so a script can
// read the detected variant without parsing stdout.
enum ChannelIndex
{
    kSse2 = 0,
    kAvx = 1,
    kAvx2 = 2,
    kAvx512 = 3,
    kAvx10_1 = 4,
    kArm64 = 5
};

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

// Resolve the best build channel for this machine. The CPUID feature bits are
// gated on the OS having actually enabled the wider register state (XGETBV/XCR0),
// so we never pick a build the OS cannot context-switch.
std::wstring detectChannel(int* outIndex)
{
    if (isArm64Native())
    {
        if (outIndex != nullptr)
            *outIndex = kArm64;
        return L"arm64-neon";
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
    const bool haveAvx = cpuAvx && osYmm;

    bool haveAvx2 = false;
    bool haveAvx512f = false;
    bool haveAvx10_1 = false;
    if (maxLeaf >= 7)
    {
        __cpuidex(info, 7, 0);
        haveAvx2 = ((info[1] & (1 << 5)) != 0) && haveAvx;       // EBX[5], needs YMM
        haveAvx512f = ((info[1] & (1 << 16)) != 0) && osZmm;     // EBX[16], needs ZMM

        __cpuidex(info, 7, 1);
        const bool avx10Enumerated = (info[3] & (1 << 19)) != 0; // EDX[19]
        if (avx10Enumerated && maxLeaf >= 0x24)
        {
            __cpuidex(info, 0x24, 0);
            const int avx10Version = info[1] & 0xFF;             // EBX[7:0]
            const bool avx10Has512 = (info[1] & (1 << 18)) != 0; // EBX[18] AVX10/512
            haveAvx10_1 = (avx10Version >= 1) && avx10Has512 && osZmm;
        }
    }

    // Most specific / newest first.
    if (haveAvx10_1)
    {
        if (outIndex != nullptr)
            *outIndex = kAvx10_1;
        return L"x64-avx10-1";
    }
    if (haveAvx512f)
    {
        if (outIndex != nullptr)
            *outIndex = kAvx512;
        return L"x64-avx512";
    }
    if (haveAvx2)
    {
        if (outIndex != nullptr)
            *outIndex = kAvx2;
        return L"x64-avx2";
    }
    if (haveAvx)
    {
        if (outIndex != nullptr)
            *outIndex = kAvx;
        return L"x64-avx";
    }
    if (outIndex != nullptr)
        *outIndex = kSse2;
    return L"x64-sse2";
}

// Per-variant installer asset name. The channel appears twice because each
// variant's packId already embeds the channel (EqualizerAPO-XT-<channel>), and
// Velopack appends "-<channel>-Setup.exe".
std::wstring assetName(const std::wstring& channel)
{
    return L"EqualizerAPO-XT-" + channel + L"-" + channel + L"-Setup.exe";
}

// Always-latest download path. GitHub redirects /releases/latest/download/<asset>
// to the newest release's asset, so this binary never needs rebuilding per release.
std::wstring latestAssetPath(const std::wstring& asset)
{
    return std::wstring(L"/") + kRepoOwner + L"/" + kRepoName +
        L"/releases/latest/download/" + asset;
}

std::wstring assetPath(const std::wstring& channel)
{
    return latestAssetPath(assetName(channel));
}

std::wstring downloadUrl(const std::wstring& channel)
{
    return std::wstring(L"https://github.com") + assetPath(channel);
}

std::wstring tempFilePath(const std::wstring& fileName)
{
    wchar_t dir[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, dir);
    if (len == 0 || len > MAX_PATH)
        return fileName;
    return std::wstring(dir) + fileName;
}

// Download github.com<path> to outFile over HTTPS. WinHTTP follows GitHub's
// redirect to the objects CDN automatically (https->https, allowed by the
// default redirect policy). Returns true on HTTP 200 + complete write.
bool downloadToFile(const std::wstring& path, const std::wstring& outFile, std::wstring& error)
{
    bool ok = false;
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;

    session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        error = L"Could not initialise WinHTTP.";
        goto cleanup;
    }

    connect = WinHttpConnect(session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect == nullptr)
    {
        error = L"Could not connect to github.com.";
        goto cleanup;
    }

    request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == nullptr)
    {
        error = L"Could not create the download request.";
        goto cleanup;
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
    {
        error = L"No response from the download server. Check your internet connection.";
        goto cleanup;
    }

    {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        if (statusCode != 200)
        {
            error = L"The matching installer was not found on the release page (HTTP " +
                std::to_wstring(statusCode) + L").";
            goto cleanup;
        }
    }

    file = CreateFileW(outFile.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = L"Could not create a temporary file for the download.";
        goto cleanup;
    }

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            error = L"The download was interrupted.";
            goto cleanup;
        }
        if (available == 0)
            break;

        std::string buffer;
        buffer.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, &buffer[0], available, &read) || read == 0)
        {
            error = L"The download was interrupted.";
            goto cleanup;
        }

        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)
        {
            error = L"Could not write the downloaded installer to disk.";
            goto cleanup;
        }
    }

    ok = true;

cleanup:
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connect != nullptr)
        WinHttpCloseHandle(connect);
    if (session != nullptr)
        WinHttpCloseHandle(session);
    if (!ok && file != INVALID_HANDLE_VALUE)
        DeleteFileW(outFile.c_str());
    return ok;
}

// Compute the SHA-256 of a file as lowercase hex using CNG. The file is
// streamed in 64 KiB chunks so the installer never has to fit in memory.
// CNG returns NTSTATUS where STATUS_SUCCESS is 0, so any nonzero status is
// treated as a failure.
bool sha256OfFile(const std::wstring& path, std::wstring& outHexLower, std::wstring& error)
{
    bool ok = false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    UCHAR digest[32] = {};
    std::string buffer;
    buffer.resize(64 * 1024);

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    {
        error = L"Could not initialise the SHA-256 provider.";
        goto cleanup;
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0)
    {
        error = L"Could not create a SHA-256 hash object.";
        goto cleanup;
    }

    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = L"Could not open the downloaded installer for verification.";
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
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(&buffer[0]), read, 0) != 0)
        {
            error = L"Could not hash the downloaded installer.";
            goto cleanup;
        }
    }

    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0)
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
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (hash != nullptr)
        BCryptDestroyHash(hash);
    if (algorithm != nullptr)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char toLowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Find fileName in sha256sum-style checksum text and return its digest as
// lowercase hex. Each line is "<64 hex chars>  <name>"; the binary-mode form
// "<hash> *<name>" is accepted too, and the name comparison ignores ASCII
// case. Returns an empty string when no line matches.
std::wstring expectedHashFromChecksums(const std::string& text, const std::wstring& fileName)
{
    std::string narrowName;
    int needed = WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, nullptr, 0,
        nullptr, nullptr);
    if (needed > 1)
    {
        narrowName.resize(needed - 1);
        WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, &narrowName[0], needed - 1,
            nullptr, nullptr);
    }
    if (narrowName.empty())
        return std::wstring();

    // Skip a UTF-8 byte order mark, in case the generator wrote one.
    size_t lineStart = 0;
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        lineStart = 3;

    while (lineStart < text.size())
    {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        while (!line.empty() &&
            (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }

        // 64 hex digits, separating whitespace, an optional '*' binary-mode
        // marker, then the asset name.
        if (line.size() < 64 + 2)
            continue;
        bool hexOk = true;
        for (size_t i = 0; i < 64; ++i)
        {
            if (!isHexDigit(line[i]))
            {
                hexOk = false;
                break;
            }
        }
        if (!hexOk || (line[64] != ' ' && line[64] != '\t'))
            continue;

        size_t nameStart = 64;
        while (nameStart < line.size() && (line[nameStart] == ' ' || line[nameStart] == '\t'))
            ++nameStart;
        if (nameStart < line.size() && line[nameStart] == '*')
            ++nameStart;
        if (nameStart >= line.size())
            continue;

        const std::string name = line.substr(nameStart);
        if (name.size() != narrowName.size())
            continue;
        bool nameMatches = true;
        for (size_t i = 0; i < name.size(); ++i)
        {
            if (toLowerAscii(name[i]) != toLowerAscii(narrowName[i]))
            {
                nameMatches = false;
                break;
            }
        }
        if (!nameMatches)
            continue;

        std::wstring hex;
        hex.reserve(64);
        for (size_t i = 0; i < 64; ++i)
            hex += static_cast<wchar_t>(toLowerAscii(line[i]));
        return hex;
    }
    return std::wstring();
}

// Read a small file fully into memory. The checksums list is at most a few
// kilobytes; refuse anything over 1 MiB so an unexpected response cannot
// balloon.
bool readSmallFile(const std::wstring& path, std::string& outData)
{
    bool ok = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 1024 * 1024)
    {
        outData.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, &outData[0], static_cast<DWORD>(outData.size()), &read, nullptr) &&
            read == outData.size();
    }

    CloseHandle(file);
    if (!ok)
        outData.clear();
    return ok;
}

// Verify the downloaded installer against the SHA256SUMS.txt asset that CI
// publishes to the same release. Returns true only when the checksums file
// downloads, lists the installer, and the SHA-256 matches.
bool verifySetupChecksum(const std::wstring& setupFile, const std::wstring& setupName,
    std::wstring& error)
{
    const std::wstring sumsFile = tempFilePath(kChecksumsAssetName);

    std::wstring downloadError;
    if (!downloadToFile(latestAssetPath(kChecksumsAssetName), sumsFile, downloadError))
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
    PROCESS_INFORMATION proc = {};

    // CreateProcessW may modify the command-line buffer, so pass a writable copy.
    std::wstring mutableCommand = commandLine;
    if (!CreateProcessW(setupPath.c_str(), &mutableCommand[0], nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &startup, &proc))
    {
        return false;
    }

    if (silent)
    {
        WaitForSingleObject(proc.hProcess, INFINITE);
        GetExitCodeProcess(proc.hProcess, &exitCode);
    }
    else
    {
        exitCode = 0;
    }

    CloseHandle(proc.hThread);
    CloseHandle(proc.hProcess);
    return true;
}

bool hasFlag(int argc, wchar_t** argv, const wchar_t* flag)
{
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], flag) == 0)
            return true;
    }
    return false;
}

const wchar_t* flagValue(int argc, wchar_t** argv, const wchar_t* flag)
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (_wcsicmp(argv[i], flag) == 0)
            return argv[i + 1];
    }
    return nullptr;
}

void writeTextFile(const wchar_t* path, const std::wstring& text)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    std::string utf8;
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1)
    {
        utf8.resize(needed - 1);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], needed - 1, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
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
        HANDLE conout = CreateFileW(L"CONOUT$", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (conout != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteConsoleW(conout, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
            CloseHandle(conout);
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
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool detectOnly = argv != nullptr && hasFlag(argc, argv, L"--detect-only");
    const bool silent = argv != nullptr && hasFlag(argc, argv, L"--silent");
    const wchar_t* outPath = argv != nullptr ? flagValue(argc, argv, L"--out") : nullptr;

    int index = kAvx2;
    const std::wstring channel = detectChannel(&index);
    const std::wstring url = downloadUrl(channel);

    if (detectOnly)
    {
        const int code = reportDetection(channel, url, outPath, index);
        if (argv != nullptr)
            LocalFree(argv);
        return code;
    }

    if (argv != nullptr)
        LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // A marquee shell progress dialog covers the detect+download gap so the user
    // is not left staring at nothing before Velopack's installer appears.
    IProgressDialog* progress = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&progress))) && progress != nullptr)
    {
        progress->SetTitle(L"EqualizerAPO-XT");
        progress->SetLine(1, L"Selecting the best build for your CPU...", FALSE, nullptr);
        progress->SetLine(2, channel.c_str(), FALSE, nullptr);
        progress->StartProgressDialog(nullptr, nullptr,
            PROGDLG_MARQUEEPROGRESS | PROGDLG_NOMINIMIZE | PROGDLG_NOCANCEL, nullptr);
    }

    const std::wstring outFile = tempFilePath(assetName(channel));
    std::wstring error;
    const bool downloaded = downloadToFile(assetPath(channel), outFile, error);

    // Check the download against the release's SHA256SUMS.txt before anything
    // is executed. A failed or impossible verification discards the download.
    bool verified = false;
    if (downloaded)
    {
        if (progress != nullptr)
            progress->SetLine(1, L"Verifying the downloaded installer...", FALSE, nullptr);
        verified = verifySetupChecksum(outFile, assetName(channel), error);
    }

    if (progress != nullptr)
    {
        progress->StopProgressDialog();
        progress->Release();
        progress = nullptr;
    }

    int result = 0;
    if (!downloaded)
    {
        const std::wstring message = error + L"\n\nYou can download a build manually from:\n" +
            kReleasesPage;
        MessageBoxW(nullptr, message.c_str(),
            L"EqualizerAPO-XT - install failed", MB_OK | MB_ICONERROR);
        result = 2;
    }
    else if (!verified)
    {
        DeleteFileW(outFile.c_str());
        const std::wstring message = error +
            L"\n\nPlease try again or download a build manually from:\n" + kReleasesPage;
        MessageBoxW(nullptr, message.c_str(),
            L"EqualizerAPO-XT - install failed", MB_OK | MB_ICONERROR);
        result = 4;
    }
    else
    {
        DWORD setupExit = 0;
        if (!launchSetup(outFile, silent, setupExit))
        {
            MessageBoxW(nullptr,
                L"The downloaded installer could not be started.",
                L"EqualizerAPO-XT - install failed", MB_OK | MB_ICONERROR);
            result = 3;
        }
        else if (silent)
        {
            result = static_cast<int>(setupExit);
        }
    }

    CoUninitialize();
    return result;
}
