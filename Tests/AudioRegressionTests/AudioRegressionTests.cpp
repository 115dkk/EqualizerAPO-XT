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
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include "engine/FilterEngine.h"
#include "services/logging/LogHelper.h"
#include "audio/io/SndfileRAII.h"
#include "text/StringHelper.h"
#include "platform/windows/Win32Resource.h"
#include "Tests/TestHarness.h"

#pragma comment(lib, "bcrypt.lib")

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
	// The engine is driven this many frames at a time, the way Windows calls the
	// APO with a fixed period. Every stateful filter here - BiQuad, Delay,
	// Convolution's partitioned overlap-add, GraphicEQ - carries state between
	// calls, and that carry-over is only observable across successive process()
	// calls. A single whole-buffer call cannot see it at all.
	//
	// Must divide frames exactly. ConvolutionFilter freezes its block length at
	// initialize() and mutes any block of a different size (ConvolutionFilter.cpp
	// frameCount mismatch guard), so a short final block would silence the tail
	// rather than test it.
	unsigned blockFrames;
};

void ensureDirectory(const std::wstring& path);

const TestCase kCases[] = {
	{ "preamp_minus6",       "preamp_minus6.txt",       SignalType::DCStereo,      48000, 2, 4800, 480 },
	{ "biquad_peaking_1khz", "biquad_peaking_1khz.txt", SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	{ "copy_crossfeed",      "copy_crossfeed.txt",      SignalType::ImpulseStereo, 48000, 2, 256,  64  },
	// 2048 frames in 256-frame blocks puts the 512-sample delay line's read and
	// write heads in different blocks, which is where a delay's ring-buffer
	// wrap-around goes wrong if it goes wrong at all.
	{ "delay_512",           "delay_512.txt",           SignalType::ImpulseStereo, 48000, 2, 2048, 256 },
	{ "graphiceq_15band",    "graphiceq_15band.txt",    SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	{ "convolution_short",   "convolution_short.txt",   SignalType::ImpulseStereo, 48000, 2, 4096, 512 },
	// Audit #250 F055: MultiConvolution had no golden case although its
	// process/mix path is entirely its own (per-mapping convolve-and-sum
	// into the target's pre-command signal). The mapping form is the fork's
	// signature grammar, so the case exercises it rather than the simple
	// form the unit tests already pin.
	{ "multiconvolution_short", "multiconvolution_short.txt", SignalType::ImpulseStereo, 48000, 2, 4096, 512 },
	{ "iir_order2_lowpass",  "iir_order2_lowpass.txt",  SignalType::ImpulseStereo, 48000, 2, 256,  64  },
	{ "channel_left_only",   "channel_left_only.txt",   SignalType::DCStereo,      48000, 2, 256,  64  },
	// LoudnessCorrection with State 0 is a deterministic pass-through. With
	// State 1 the filter reads the live system master volume (VolumeController)
	// and runs a background parameter thread, so its output is not stable
	// enough for a stored reference; State 0 keeps the factory + parameter
	// parsing covered without that non-determinism.
	{ "loudnesscorrection_bypassed", "loudnesscorrection_bypassed.txt", SignalType::ImpulseStereo, 48000, 2, 256, 64 },
	// All-pass, added for the reform in issue #228. An all-pass is flat, so an
	// impulse response is the only thing that can catch a change in it: the
	// whole filter lives in where the energy lands in time, not in how much
	// there is. These three baselines exist so the Editor-side work can prove
	// it left the engine alone.
	//
	// q0707 is the width the reform makes the default for new filters, q10 is
	// the width the current template creates and existing configs therefore
	// carry, and bw1oct is the case the Editor silently rewrites to "Q 1"
	// today - the one baseline that has to change meaning only when the fix
	// lands, and not before.
	//
	// 8192 frames at 512 covers the ring-down: the longest of the three is
	// bw1oct, whose group delay at 80 Hz is around 540 samples.
	{ "allpass_q0707",       "allpass_q0707.txt",       SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	{ "allpass_q10",         "allpass_q10.txt",         SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	{ "allpass_bw1oct",      "allpass_bw1oct.txt",      SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	// The 1st-order section, which the config grammar reaches through
	// "Order 1". It is not a preset of the 2nd-order one: no choice of Q makes
	// a 2nd-order section turn half a circle, so this is the only way to get a
	// 90-degree crossing.
	{ "allpass_order1_fc100",  "allpass_order1_fc100.txt",  SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	{ "allpass_order1_fc1000", "allpass_order1_fc1000.txt", SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	// The identity that anchors the whole 1st-order derivation, at the level a
	// user would hear it: two 1st-order sections at one frequency are one
	// 2nd-order section at Q 0.5. These two cases are separate configurations
	// whose baselines are byte-identical to each other, which is a stronger
	// statement than either baseline alone. HybridConvTests proves the same
	// thing on the coefficients.
	{ "allpass_order1_x2",     "allpass_order1_x2.txt",     SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	{ "allpass_order2_q05",    "allpass_order2_q05.txt",    SignalType::ImpulseStereo, 48000, 2, 8192, 512 },
	// Spatial filters added as independent commands. Hilbert proves that one
	// channel receives the 1025-tap quadrature FIR while the other receives
	// the matching 512-sample latency. Static Velvet pins the sparse,
	// unit-energy kernels; Dynamic Velvet uses DC long enough to cross two
	// renewal boundaries and therefore pins the equal-power transition path.
	{ "hilbert_roles",         "hilbert_roles.txt",         SignalType::ImpulseStereo, 48000, 2, 4096, 512 },
	{ "velvet_static",         "velvet_static.txt",         SignalType::ImpulseStereo, 48000, 2, 4096, 256 },
	{ "velvet_dynamic",        "velvet_dynamic.txt",        SignalType::DCStereo,      48000, 2, 12288, 256 },
};

bool writeCaseManifest(const std::wstring& directory)
{
	ensureDirectory(directory);
	const std::wstring path = directory + L"\\cases.json";
	FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr)
		return false;
	fputs("{\n  \"cases\": [\n", file);
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
		fprintf(file, "    \"%s\"%s\n", kCases[i].name,
			i + 1 == sizeof(kCases) / sizeof(kCases[0]) ? "" : ",");
	fputs("  ]\n}\n", file);
	return fclose(file) == 0;
}

struct Options
{
	std::string variant = "default";
	bool generateMode = false;
	std::wstring refDir;
	std::wstring configDir;
	std::wstring outDir;
	double toleranceDb = -120.0;
	// Extra impulse-response files for the MultiConvolution equivalence
	// battery (e.g. a real Impulcifer hrir.wav); the synthetic battery always
	// runs regardless.
	std::vector<std::wstring> equivIrFiles;
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
		else if (a == "--equiv-ir") o.equivIrFiles.push_back(toWide(next()));
		else if (a == "--help" || a == "-h") {
			printf("Usage: AudioRegressionTests [options]\n");
			printf("  --variant <name>          Tag for the output subdirectory (default: \"default\")\n");
			printf("  --generate-references     Write current outputs as the reference set\n");
			printf("  --ref-dir <path>          Override reference directory\n");
			printf("  --config-dir <path>       Override config directory\n");
			printf("  --out-dir <path>          Override per-variant output directory\n");
			printf("  --tolerance-db <db>       Compare tolerance in dBFS (default: -120)\n");
			printf("  --equiv-ir <path>         Also run the MultiConvolution equivalence battery\n");
			printf("                            against this impulse response file (repeatable)\n");
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
	winutil::UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file) return false;
	DWORD written = 0;
	BOOL ok = WriteFile(file.get(), data.data(), (DWORD)(data.size() * sizeof(float)), &written, nullptr);
	return ok && written == data.size() * sizeof(float);
}

bool readRawFloat(const std::wstring& path, std::vector<float>& out)
{
	winutil::UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file) return false;
	LARGE_INTEGER size;
	if (!GetFileSizeEx(file.get(), &size) || size.QuadPart % sizeof(float) != 0) return false;
	out.resize((size_t)(size.QuadPart / sizeof(float)));
	DWORD readBytes = 0;
	BOOL ok = ReadFile(file.get(), out.data(), (DWORD)(out.size() * sizeof(float)), &readBytes, nullptr);
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

// ---------------------------------------------------------------------------
// MultiConvolution equivalence battery
//
// Proves that the one-line "MultiConvolution: L=<f0>*0+<f1>*1+... <ir>" is
// bit-identical (SHA-256 over the raw float output) to the manual fan-out it
// compresses:
//
//   Copy: V1=L V2=L ...
//   Channel: L V1 V2 ...
//   Convolution: <ir>          (selected channel i gets file channel i)
//   Copy: L=<f0>*L+<f1>*V1+...
//   Channel: ALL
//
// Both pipelines run the same hcInitSingle/hcPut/hcProcess/hcGet sequence per
// (input, IR channel) unit at the same block length and sum in the same order,
// so the equality is exact, not within a tolerance. The battery always runs on
// deterministic synthetic impulse responses (2 and 8 channels); --equiv-ir
// adds real files (e.g. an Impulcifer hrir.wav) on top.

std::string sha256Hex(const void* data, size_t size)
{
	UniqueBCryptAlgorithm algorithm;
	UniqueBCryptHash hash;
	UCHAR digest[32] = {};
	std::string hex;
	if (BCryptOpenAlgorithmProvider(algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
	{
		if (BCryptCreateHash(algorithm.get(), hash.put(), nullptr, 0, nullptr, 0, 0) == 0)
		{
			BCryptHashData(hash.get(), (PUCHAR)data, (ULONG)size, 0);
			BCryptFinishHash(hash.get(), digest, sizeof(digest), 0);
			char buf[3];
			for (UCHAR b : digest)
			{
				snprintf(buf, sizeof(buf), "%02x", b);
				hex += buf;
			}
		}
	}
	return hex;
}

// Deterministic decaying noise per channel, seeded by the channel index, so
// every run and every SIMD variant sees the same impulse responses.
std::vector<std::vector<double>> makeSyntheticIr(unsigned channels, unsigned frames)
{
	std::vector<std::vector<double>> ir(channels, std::vector<double>(frames));
	for (unsigned c = 0; c < channels; ++c)
	{
		uint32_t state = 0x9E3779B9u * (c + 1);
		for (unsigned f = 0; f < frames; ++f)
		{
			state = state * 1664525u + 1013904223u;
			double noise = ((double)state / 4294967296.0) * 2.0 - 1.0;
			double decay = std::exp(-4.0 * (double)f / (double)frames);
			ir[c][f] = 0.5 * noise * decay;
		}
	}
	return ir;
}

bool writeIrWav(const std::wstring& path, const std::vector<std::vector<double>>& channels, unsigned sampleRate)
{
	const unsigned numCh = (unsigned)channels.size();
	const unsigned frames = (unsigned)channels[0].size();
	std::vector<double> interleaved((size_t)frames * numCh);
	for (unsigned f = 0; f < frames; ++f)
		for (unsigned c = 0; c < numCh; ++c)
			interleaved[(size_t)f * numCh + c] = channels[c][f];

	SF_INFO info = {};
	info.samplerate = (int)sampleRate;
	info.channels = (int)numCh;
	info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;
	sndfile::Handle file(sf_wchar_open(path.c_str(), SFM_WRITE, &info));
	if (!file)
		return false;
	sf_writef_double(file.get(), interleaved.data(), (sf_count_t)frames);
	return true;
}

bool writeTextFile(const std::wstring& path, const std::wstring& text)
{
	std::string utf8;
	if (!text.empty())
	{
		int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		utf8.resize((size_t)n);
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), utf8.data(), n, nullptr, nullptr);
	}
	winutil::UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return false;
	DWORD written = 0;
	BOOL ok = WriteFile(file.get(), utf8.data(), (DWORD)utf8.size(), &written, nullptr);
	return ok && written == utf8.size();
}

// "%g" like the command serializers; only ever used next to '*'.
std::wstring formatFactor(double factor)
{
	wchar_t buffer[64];
	swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", factor);
	return buffer;
}

struct EquivSpec
{
	const char* name = "";
	unsigned irChannelsUsed = 0;  // how many file channels the mapping sums
	std::vector<double> factors;  // per used file channel; empty = all unity
	unsigned repeats = 1;         // stacked blocks/lines on the same target
	bool secondTarget = false;    // also process R with file channels 0+1
};

double specFactor(const EquivSpec& spec, unsigned c)
{
	return c < spec.factors.size() ? spec.factors[c] : 1.0;
}

// The manual fan-out block for one target; factors[i] scales file channel i's
// share exactly like the MultiConvolution summand factor does.
std::wstring manualBlock(const EquivSpec& spec, const std::wstring& target, unsigned channelsUsed, const std::wstring& irPath, bool useFactors)
{
	std::wstring text;
	if (channelsUsed > 1)
	{
		text += L"Copy:";
		for (unsigned c = 1; c < channelsUsed; ++c)
			text += L" V" + std::to_wstring(c) + L"=" + target;
		text += L"\r\n";
	}
	text += L"Channel: " + target;
	for (unsigned c = 1; c < channelsUsed; ++c)
		text += L" V" + std::to_wstring(c);
	text += L"\r\n";
	text += L"Convolution: " + irPath + L"\r\n";
	text += L"Copy: " + target + L"=";
	for (unsigned c = 0; c < channelsUsed; ++c)
	{
		if (c > 0)
			text += L"+";
		const double factor = useFactors ? specFactor(spec, c) : 1.0;
		if (factor != 1.0)
			text += formatFactor(factor) + L"*";
		text += c == 0 ? target : L"V" + std::to_wstring(c);
	}
	text += L"\r\nChannel: ALL\r\n";
	return text;
}

std::wstring multiConvLine(const EquivSpec& spec, const std::wstring& target, unsigned channelsUsed, const std::wstring& irPath, bool useFactors)
{
	std::wstring text = L"MultiConvolution: " + target + L"=";
	for (unsigned c = 0; c < channelsUsed; ++c)
	{
		if (c > 0)
			text += L"+";
		const double factor = useFactors ? specFactor(spec, c) : 1.0;
		if (factor != 1.0)
			text += formatFactor(factor) + L"*";
		text += std::to_wstring(c);
	}
	text += L" " + irPath + L"\r\n";
	return text;
}

std::vector<float> makeNoiseInput(unsigned channels, unsigned frames)
{
	std::vector<float> input((size_t)frames * channels);
	uint32_t state = 0x2545F491u;
	for (float& sample : input)
	{
		state = state * 1664525u + 1013904223u;
		sample = (float)((((double)state / 4294967296.0) * 2.0 - 1.0) * 0.5);
	}
	return input;
}

// Takes the input by value: each run works on its own copy, so one engine
// touching its input buffer could never leak into the other pipeline.
std::vector<float> runEngineOverBlocks(const std::wstring& configPath, unsigned channels, unsigned sampleRate,
	unsigned blockFrames, unsigned blockCount, std::vector<float> input)
{
	std::vector<float> output(input.size(), 0.0f);
	FilterEngine engine;
	std::wstring deviceName = L"AudioRegressionTests";
	std::wstring connectionName = L"File";
	EngineSetup setup;
	setup.sampleRate = (float)sampleRate;
	setup.inputChannelCount = channels;
	setup.realChannelCount = channels;
	setup.outputChannelCount = channels;
	setup.maxFrameCount = blockFrames;
	setup.customPath = configPath;
	setup.deviceName = deviceName;
	setup.connectionName = connectionName;
	engine.initialize(setup);
	for (unsigned b = 0; b < blockCount; ++b)
	{
		size_t offset = (size_t)b * blockFrames * channels;
		engine.process(output.data() + offset, input.data() + offset, blockFrames);
	}
	return output;
}

bool runEquivalenceCase(const EquivSpec& spec, const std::string& irLabel, const std::wstring& irPath,
	unsigned irFileChannels, unsigned irFrames, unsigned sampleRate, const Options& opts, bool& outFailed)
{
	const unsigned channels = 2;
	const unsigned blockFrames = 1024;
	// Enough blocks to play past the impulse response's tail (the historic
	// "reverb dies mid-way" class of bug), bounded for very long files.
	const unsigned blockCount = std::min(256u, irFrames / blockFrames + 9);
	const unsigned channelsUsed = std::min(spec.irChannelsUsed, irFileChannels);
	const bool useFactors = !spec.factors.empty();

	std::wstring equivDir = opts.outDir + L"\\equiv";
	ensureDirectory(equivDir);
	const std::wstring caseName = toWide(irLabel) + L"_" + toWide(spec.name);

	std::wstring manualText;
	std::wstring multiText;
	for (unsigned r = 0; r < spec.repeats; ++r)
	{
		manualText += manualBlock(spec, L"L", channelsUsed, irPath, useFactors);
		multiText += multiConvLine(spec, L"L", channelsUsed, irPath, useFactors);
	}
	if (spec.secondTarget)
	{
		manualText += manualBlock(spec, L"R", std::min(2u, channelsUsed), irPath, false);
		multiText += multiConvLine(spec, L"R", std::min(2u, channelsUsed), irPath, false);
	}

	const std::wstring manualPath = equivDir + L"\\" + caseName + L"_manual.txt";
	const std::wstring multiPath = equivDir + L"\\" + caseName + L"_multiconv.txt";
	if (!writeTextFile(manualPath, manualText) || !writeTextFile(multiPath, multiText))
	{
		fprintf(stderr, "  ERROR: could not write equivalence configs in %S\n", equivDir.c_str());
		outFailed = true;
		return false;
	}

	printf("\n[equiv %s_%s] file channels used=%u repeats=%u blocks=%u\n",
		irLabel.c_str(), spec.name, channelsUsed, spec.repeats, blockCount);

	std::vector<float> input = makeNoiseInput(channels, blockFrames * blockCount);
	std::vector<float> outManual;
	std::vector<float> outMulti;
	try
	{
		outManual = runEngineOverBlocks(manualPath, channels, sampleRate, blockFrames, blockCount, input);
		outMulti = runEngineOverBlocks(multiPath, channels, sampleRate, blockFrames, blockCount, input);
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "  ERROR: %s\n", e.what());
		outFailed = true;
		return false;
	}

	// A failed IR load would make both pipelines quietly diverge or go
	// silent. Check the processed channel (L, slot 0) alone: the untouched R
	// passthrough must not be able to satisfy this, and an inversion pair that
	// cancels to silence on both sides must not "pass" either.
	double maxAbs = 0.0;
	for (size_t f = 0; f < outManual.size(); f += channels)
		maxAbs = std::max(maxAbs, std::fabs((double)outManual[f]));
	const bool audible = maxAbs > 1.0e-9;

	const std::string hashManual = sha256Hex(outManual.data(), outManual.size() * sizeof(float));
	const std::string hashMulti = sha256Hex(outMulti.data(), outMulti.size() * sizeof(float));
	const bool identical = !hashManual.empty() && hashManual == hashMulti;

	printf("  manual    SHA-256 %s\n", hashManual.c_str());
	printf("  multiconv SHA-256 %s\n", hashMulti.c_str());
	const bool passed = identical && audible;
	printf("  %s%s\n", passed ? "PASS" : "FAIL", audible ? "" : "  (output is silent)");
	if (!passed)
		outFailed = true;
	return passed;
}

// Runs every spec that the file's channel count supports; returns (passed,
// total) through the counters.
void runEquivalenceBattery(const std::string& irLabel, const std::wstring& irPath, unsigned irFileChannels,
	unsigned irFrames, unsigned sampleRate, const Options& opts, bool& outFailed, unsigned& passed, unsigned& total)
{
	const std::vector<EquivSpec> specs = {
		{"basic", 2, {}, 1, false},
		{"wide", 16, {}, 1, false},
		{"chained", 2, {}, 2, true},
		{"factor_half", 2, {0.5, 1.0}, 1, false},
		{"invert", 2, {-1.0, 1.0}, 1, false},
		{"invert_half", 2, {-0.5, 1.0}, 1, false},
	};

	for (const EquivSpec& spec : specs)
	{
		// "wide" only says something new when the file offers more than the
		// two channels "basic" already covers.
		if (spec.name == std::string("wide") && irFileChannels <= 2)
			continue;
		++total;
		if (runEquivalenceCase(spec, irLabel, irPath, irFileChannels, irFrames, sampleRate, opts, outFailed))
			++passed;
	}
}

void runAllEquivalenceBatteries(const Options& opts, bool& outFailed, unsigned& passed, unsigned& total)
{
	const unsigned sampleRate = 48000;
	const unsigned synthFrames = 2048;
	std::wstring equivDir = opts.outDir + L"\\equiv";
	ensureDirectory(equivDir);
	// The IR paths go into config lines, and the engine resolves relative
	// paths against the config file's own directory; absolute paths keep the
	// configs and the IR files independent of both that rule and the cwd.
	{
		wchar_t absolute[MAX_PATH];
		DWORD n = GetFullPathNameW(equivDir.c_str(), MAX_PATH, absolute, nullptr);
		if (n > 0 && n < MAX_PATH)
			equivDir = absolute;
	}

	const struct { const char* label; unsigned channels; } synth[] = {
		{"syn2", 2},
		{"syn8", 8},
	};
	for (const auto& s : synth)
	{
		const std::wstring irPath = equivDir + L"\\" + toWide(s.label) + L"_ir.wav";
		if (!writeIrWav(irPath, makeSyntheticIr(s.channels, synthFrames), sampleRate))
		{
			fprintf(stderr, "ERROR: could not write synthetic IR %S\n", irPath.c_str());
			outFailed = true;
			++total;
			continue;
		}
		runEquivalenceBattery(s.label, irPath, s.channels, synthFrames, sampleRate, opts, outFailed, passed, total);
	}

	unsigned fileIndex = 0;
	for (const std::wstring& irPath : opts.equivIrFiles)
	{
		++fileIndex;
		SF_INFO info = {};
		sndfile::Handle file(sf_wchar_open(irPath.c_str(), SFM_READ, &info));
		if (!file)
		{
			fprintf(stderr, "ERROR: --equiv-ir file unreadable: %S\n", irPath.c_str());
			outFailed = true;
			++total;
			continue;
		}
		const std::string label = "ir" + std::to_string(fileIndex);
		printf("\n--equiv-ir %S: %d channels, %lld frames, %d Hz\n", irPath.c_str(), info.channels, (long long)info.frames, info.samplerate);
		runEquivalenceBattery(label, irPath, (unsigned)info.channels, (unsigned)info.frames, (unsigned)info.samplerate, opts, outFailed, passed, total);
	}
}

bool runCase(const TestCase& tc, const Options& opts, bool& outFailed)
{
	std::wstring configPath = opts.configDir + L"\\" + toWide(tc.configFile);
	std::wstring refPath = opts.refDir + L"\\" + toWide(tc.name) + L".raw";
	std::wstring outVariantDir = opts.outDir + L"\\" + toWide(opts.variant);
	ensureDirectory(outVariantDir);
	std::wstring outPath = outVariantDir + L"\\" + toWide(tc.name) + L".raw";

	printf("\n[%s] config=%S frames=%u channels=%u block=%u\n", tc.name, configPath.c_str(), tc.frames, tc.channels, tc.blockFrames);

	// A short final block would be muted by the convolution filters rather than
	// processed, which would quietly weaken the case instead of failing it.
	if (tc.blockFrames == 0 || tc.frames % tc.blockFrames != 0)
	{
		fprintf(stderr, "  ERROR: blockFrames %u does not divide frames %u\n", tc.blockFrames, tc.frames);
		outFailed = true;
		return false;
	}

	std::vector<float> input = generateSignal(tc.inputType, tc.sampleRate, tc.channels, tc.frames);
	std::vector<float> output((size_t)tc.frames * tc.channels, 0.0f);

	try
	{
		FilterEngine engine;
		std::wstring deviceName = L"AudioRegressionTests";
		std::wstring connectionName = L"File";
		std::wstring deviceGuid = L"";
		EngineSetup setup;
		setup.deviceName = deviceName;
		setup.connectionName = connectionName;
		setup.deviceGuid = deviceGuid;
		// maxFrameCount is the block size, not the signal length: that is what the
		// APO passes and what the convolution filters partition against.
		setup.sampleRate = (float)tc.sampleRate;
		setup.inputChannelCount = tc.channels;
		setup.realChannelCount = tc.channels;
		setup.outputChannelCount = tc.channels;
		setup.maxFrameCount = tc.blockFrames;
		setup.customPath = configPath;
		engine.initialize(setup);
		for (unsigned offset = 0; offset < tc.frames; offset += tc.blockFrames)
		{
			const size_t sampleOffset = (size_t)offset * tc.channels;
			engine.process(output.data() + sampleOffset, input.data() + sampleOffset, tc.blockFrames);
		}
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

int runAudioRegressionTests(int argc, char** argv)
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
	const std::wstring outVariantDir = opts.outDir + L"\\" + toWide(opts.variant);
	if (!writeCaseManifest(outVariantDir))
	{
		fprintf(stderr, "  ERROR: could not write case manifest to %S\n", outVariantDir.c_str());
		anyFailed = true;
	}
	if (opts.generateMode && !writeCaseManifest(opts.refDir))
	{
		fprintf(stderr, "  ERROR: could not write reference case manifest to %S\n", opts.refDir.c_str());
		anyFailed = true;
	}
	unsigned passed = 0;
	unsigned total = 0;
	for (const auto& tc : kCases)
	{
		++total;
		if (runCase(tc, opts, anyFailed))
			++passed;
	}

	// Self-contained (no stored references), so it runs in both modes.
	runAllEquivalenceBatteries(opts, anyFailed, passed, total);

	printf("\nSummary: %u/%u passed", passed, total);
	if (anyFailed)
		printf("  (FAILURES)\n");
	else
		printf("\n");

	// Route the final verdict through the shared harness. Under the harness
	// default (Collect) it is report() that carries the exit code: it prints
	// the failure summary to stderr and exits(1), preserving the previous
	// "return anyFailed ? 1 : 0" exit semantics. Without the report() call a
	// failed run would return 0. anyFailed covers both a tolerance drift in
	// verify mode and a reference-write error in generate mode.
	test::Harness harness("AudioRegressionTests");
	harness.expect(!anyFailed, "one or more regression cases failed (drift beyond tolerance or I/O error)");
	harness.report();
	return 0;
}

int main(int argc, char** argv)
{
	try
	{
		return runAudioRegressionTests(argc, argv);
	}
	catch (const std::exception& error)
	{
		fprintf(stderr, "AudioRegressionTests: unhandled exception: %s\n", error.what());
	}
	catch (...)
	{
		fprintf(stderr, "AudioRegressionTests: unhandled non-standard exception\n");
	}
	return EXIT_FAILURE;
}
