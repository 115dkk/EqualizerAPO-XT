/*
	This file is part of EqualizerAPO-XT.

	Audio regression test harness. Drives FilterEngine through a fixed set
	of small DSP scenarios and either records the float interleaved output
	as a reference, or compares it to a stored reference within a tolerance.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "FilterEngine.h"
#include "helpers/LogHelper.h"
#include "helpers/StringHelper.h"

namespace
{

enum class SignalType
{
	Impulse,
	ImpulseStereo,
	Sine1k,
	DC,
	DCStereo,
};

struct TestCase
{
	const char* name;
	const char* configFile;
	SignalType inputType;
	unsigned sampleRate;
	unsigned channels;
	unsigned frames;
};

const TestCase kCases[] = {
	{ "preamp_minus6",       "preamp_minus6.txt",       SignalType::DCStereo,      48000, 2, 4800  },
	{ "biquad_peaking_1khz", "biquad_peaking_1khz.txt", SignalType::ImpulseStereo, 48000, 2, 8192  },
	{ "copy_crossfeed",      "copy_crossfeed.txt",      SignalType::ImpulseStereo, 48000, 2, 256   },
	{ "delay_512",           "delay_512.txt",           SignalType::ImpulseStereo, 48000, 2, 2048  },
	{ "graphiceq_15band",    "graphiceq_15band.txt",    SignalType::ImpulseStereo, 48000, 2, 8192  },
	{ "convolution_short",   "convolution_short.txt",   SignalType::ImpulseStereo, 48000, 2, 4096  },
};

struct Options
{
	std::string variant = "default";
	bool generateMode = false;
	std::wstring refDir;
	std::wstring configDir;
	std::wstring outDir;
	double toleranceDb = -120.0;
};

std::wstring toWide(const std::string& s)
{
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
	return w;
}

std::wstring exeDirectory()
{
	wchar_t buf[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring path(buf, n);
	size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		path.resize(slash);
	return path;
}

void ensureDirectory(const std::wstring& path)
{
	if (path.empty())
		return;

	std::wstring normalized = path;
	std::replace(normalized.begin(), normalized.end(), L'/', L'\\');

	size_t start = 0;
	if (normalized.size() >= 3 && normalized[1] == L':' && normalized[2] == L'\\')
	{
		start = 3;
	}
	else if (normalized.rfind(L"\\\\", 0) == 0)
	{
		size_t serverEnd = normalized.find(L'\\', 2);
		if (serverEnd == std::wstring::npos)
			return;
		size_t shareEnd = normalized.find(L'\\', serverEnd + 1);
		if (shareEnd == std::wstring::npos)
			return;
		start = shareEnd + 1;
	}

	for (size_t pos = start; pos <= normalized.size(); )
	{
		size_t next = normalized.find(L'\\', pos);
		std::wstring part = next == std::wstring::npos ? normalized : normalized.substr(0, next);
		if (!part.empty())
			CreateDirectoryW(part.c_str(), nullptr);
		if (next == std::wstring::npos)
			break;
		pos = next + 1;
	}
}

Options parseOptions(int argc, char** argv)
{
	Options o;
	std::wstring exeDir = exeDirectory();
	o.refDir = exeDir + L"\\references";
	o.configDir = exeDir + L"\\configs";
	o.outDir = exeDir + L"\\output";

	for (int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		auto next = [&]() -> std::string {
			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for %s\n", a.c_str());
				exit(2);
			}
			return argv[++i];
		};

		if (a == "--variant") o.variant = next();
		else if (a == "--generate-references") o.generateMode = true;
		else if (a == "--ref-dir") o.refDir = toWide(next());
		else if (a == "--config-dir") o.configDir = toWide(next());
		else if (a == "--out-dir") o.outDir = toWide(next());
		else if (a == "--tolerance-db") o.toleranceDb = std::atof(next().c_str());
		else if (a == "--help" || a == "-h") {
			printf("Usage: AudioRegressionTests [options]\n");
			printf("  --variant <name>          Tag for the output subdirectory (default: \"default\")\n");
			printf("  --generate-references     Write current outputs as the reference set\n");
			printf("  --ref-dir <path>          Override reference directory\n");
			printf("  --config-dir <path>       Override config directory\n");
			printf("  --out-dir <path>          Override per-variant output directory\n");
			printf("  --tolerance-db <db>       Compare tolerance in dBFS (default: -120)\n");
			exit(0);
		}
		else {
			fprintf(stderr, "Unknown argument: %s\n", a.c_str());
			exit(2);
		}
	}
	return o;
}

std::vector<float> generateSignal(SignalType type, unsigned sampleRate, unsigned channels, unsigned frames)
{
	std::vector<float> buf((size_t)frames * channels, 0.0f);
	switch (type)
	{
	case SignalType::Impulse:
		buf[0] = 1.0f;
		break;
	case SignalType::ImpulseStereo:
		for (unsigned c = 0; c < channels; ++c)
			buf[c] = 1.0f;
		break;
	case SignalType::Sine1k: {
		double w = 2.0 * 3.14159265358979323846 * 1000.0 / sampleRate;
		for (unsigned i = 0; i < frames; ++i) {
			float s = (float)std::sin(w * i);
			for (unsigned c = 0; c < channels; ++c)
				buf[(size_t)i * channels + c] = s;
		}
		break;
	}
	case SignalType::DC:
		for (size_t i = 0; i < buf.size(); ++i) buf[i] = 1.0f;
		break;
	case SignalType::DCStereo:
		for (size_t i = 0; i < buf.size(); ++i) buf[i] = 1.0f;
		break;
	}
	return buf;
}

bool writeRawFloat(const std::wstring& path, const std::vector<float>& data)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	BOOL ok = WriteFile(h, data.data(), (DWORD)(data.size() * sizeof(float)), &written, nullptr);
	CloseHandle(h);
	return ok && written == data.size() * sizeof(float);
}

bool readRawFloat(const std::wstring& path, std::vector<float>& out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size;
	GetFileSizeEx(h, &size);
	if (size.QuadPart % sizeof(float) != 0) { CloseHandle(h); return false; }
	out.resize((size_t)(size.QuadPart / sizeof(float)));
	DWORD readBytes = 0;
	BOOL ok = ReadFile(h, out.data(), (DWORD)(out.size() * sizeof(float)), &readBytes, nullptr);
	CloseHandle(h);
	return ok && readBytes == out.size() * sizeof(float);
}

struct CompareResult
{
	bool passed;
	double maxAbsError;
	size_t maxErrorIndex;
	double rmse;
	double snrDb;
	size_t sampleCount;
};

CompareResult compareBuffers(const std::vector<float>& out, const std::vector<float>& ref, double toleranceDb)
{
	CompareResult r{};
	r.sampleCount = std::min(out.size(), ref.size());

	if (out.size() != ref.size())
	{
		r.passed = false;
		r.maxAbsError = std::numeric_limits<double>::infinity();
		return r;
	}

	double tolerance = std::pow(10.0, toleranceDb / 20.0);
	double sumSqError = 0.0;
	double sumSqSignal = 0.0;

	for (size_t i = 0; i < r.sampleCount; ++i)
	{
		double err = std::fabs((double)out[i] - (double)ref[i]);
		if (err > r.maxAbsError) {
			r.maxAbsError = err;
			r.maxErrorIndex = i;
		}
		sumSqError += err * err;
		sumSqSignal += (double)ref[i] * (double)ref[i];
	}

	r.rmse = std::sqrt(sumSqError / (double)r.sampleCount);
	if (sumSqError > 0 && sumSqSignal > 0)
		r.snrDb = 10.0 * std::log10(sumSqSignal / sumSqError);
	else
		r.snrDb = std::numeric_limits<double>::infinity();

	r.passed = r.maxAbsError <= tolerance;
	return r;
}

bool runCase(const TestCase& tc, const Options& opts, bool& outFailed)
{
	std::wstring configPath = opts.configDir + L"\\" + toWide(tc.configFile);
	std::wstring refPath = opts.refDir + L"\\" + toWide(tc.name) + L".raw";
	std::wstring outVariantDir = opts.outDir + L"\\" + toWide(opts.variant);
	ensureDirectory(outVariantDir);
	std::wstring outPath = outVariantDir + L"\\" + toWide(tc.name) + L".raw";

	printf("\n[%s] config=%S frames=%u channels=%u\n", tc.name, configPath.c_str(), tc.frames, tc.channels);

	std::vector<float> input = generateSignal(tc.inputType, tc.sampleRate, tc.channels, tc.frames);
	std::vector<float> output((size_t)tc.frames * tc.channels, 0.0f);

	try
	{
		FilterEngine engine;
		std::wstring deviceName = L"AudioRegressionTests";
		std::wstring connectionName = L"File";
		std::wstring deviceGuid = L"";
		std::wstring deviceString = deviceName + L" " + connectionName + L" " + deviceGuid;
		engine.setDeviceInfo(false, true, deviceName, connectionName, deviceGuid, deviceString);
		engine.initialize((float)tc.sampleRate, tc.channels, tc.channels, tc.channels, 0, tc.frames, configPath);
		engine.process(output.data(), input.data(), tc.frames);
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "  ERROR: %s\n", e.what());
		outFailed = true;
		return false;
	}

	if (!writeRawFloat(outPath, output))
		fprintf(stderr, "  WARNING: could not write %S\n", outPath.c_str());

	if (opts.generateMode)
	{
		ensureDirectory(opts.refDir);
		if (!writeRawFloat(refPath, output)) {
			fprintf(stderr, "  ERROR: could not write reference %S\n", refPath.c_str());
			outFailed = true;
			return false;
		}
		printf("  generated reference (%zu samples)\n", output.size());
		return true;
	}

	std::vector<float> reference;
	if (!readRawFloat(refPath, reference))
	{
		fprintf(stderr, "  ERROR: reference missing or unreadable: %S\n", refPath.c_str());
		fprintf(stderr, "         (run with --generate-references to create it)\n");
		outFailed = true;
		return false;
	}

	CompareResult cr = compareBuffers(output, reference, opts.toleranceDb);
	const char* verdict = cr.passed ? "PASS" : "FAIL";
	printf("  %s  maxAbsError=%.3e (at %zu)  rmse=%.3e  snr=%.2f dB\n",
		verdict, cr.maxAbsError, cr.maxErrorIndex, cr.rmse, cr.snrDb);
	if (!cr.passed) outFailed = true;
	return cr.passed;
}

}

int main(int argc, char** argv)
{
	LogHelper::set(stderr, false, false, false);

	Options opts = parseOptions(argc, argv);

	printf("AudioRegressionTests\n");
	printf("  variant     = %s\n", opts.variant.c_str());
	printf("  config dir  = %S\n", opts.configDir.c_str());
	printf("  ref dir     = %S\n", opts.refDir.c_str());
	printf("  out dir     = %S\n", opts.outDir.c_str());
	printf("  tolerance   = %.1f dB\n", opts.toleranceDb);
	printf("  mode        = %s\n", opts.generateMode ? "GENERATE" : "VERIFY");
	printf("  cases       = %zu\n", sizeof(kCases) / sizeof(kCases[0]));

	bool anyFailed = false;
	unsigned passed = 0;
	unsigned total = 0;
	for (const auto& tc : kCases)
	{
		++total;
		if (runCase(tc, opts, anyFailed))
			++passed;
	}

	printf("\nSummary: %u/%u passed", passed, total);
	if (anyFailed)
		printf("  (FAILURES)\n");
	else
		printf("\n");

	return anyFailed ? 1 : 0;
}
