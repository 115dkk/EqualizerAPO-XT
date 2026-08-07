#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"
#include "SubwooferRouting/State.h"
#include "Tests/TestHarness.h"

namespace
{

test::Harness harness("SubwooferRoutingEngineTests");

constexpr unsigned kSampleRate = 48000;
constexpr unsigned kBlockFrames = 512;
constexpr unsigned kFiveChannelMask = 0x3B;
constexpr unsigned kSixChannelMask = 0x3F;
constexpr unsigned kFiveChannels = 5;
constexpr unsigned kSixChannels = 6;

// Ninety complete 512-frame blocks. This is the permitted 46080-frame
// alternative to processing 93 complete blocks and ignoring the final 384
// frames of a nominal 48000-frame signal.
constexpr unsigned kNoiseFrames = 46080;
constexpr unsigned kNoiseBlockCount = kNoiseFrames / kBlockFrames;

constexpr char kOriginalConfig[] =
	"Copy: XL=L XR=R XRL=RL XRR=RR XFLFE=-1.0*L+-1.0*R XRLFE=-1.0*RL+-1.0*RR\n"
	"Channel: XL XR\n"
	"Delay: 2.5 ms\n"
	"Filter: ON HPQ Fc 80 Hz Q 0.707\n"
	"Channel: XFLFE\n"
	"Filter: ON LPQ Fc 80 Hz Q 0.707\n"
	"Filter: ON LPQ Fc 80 Hz Q 0.707\n"
	"Channel: XRL XRR\n"
	"Delay: 2 ms\n"
	"Filter: ON HPQ Fc 100 Hz Q 0.707\n"
	"Channel: XRLFE\n"
	"Filter: ON LPQ Fc 60 Hz Q 0.707\n"
	"Channel: LFE\n"
	"Preamp: 10 dB\n"
	"Copy: L=LFE+XL+XFLFE R=LFE+XR+XFLFE LFE=-14.0dB*LFE+-14.0dB*XFLFE+XRLFE RL=XRL RR=XRR\n";

bool writeUtf8File(const std::wstring& path, const std::string& text)
{
	FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr)
		return false;

	const size_t written = std::fwrite(text.data(), 1, text.size(), file);
	const int closeResult = std::fclose(file);
	return written == text.size() && closeResult == 0;
}

bool isPureAscii(const std::string& text)
{
	return std::all_of(text.begin(), text.end(), [](unsigned char value)
	{
		return value <= 0x7F;
	});
}

bool bitEqual(double left, double right)
{
	return std::memcmp(&left, &right, sizeof(double)) == 0;
}

std::vector<double> makeNoise(unsigned channels, unsigned frames)
{
	std::vector<double> input(static_cast<size_t>(channels) * frames);
	std::uint64_t state = 0x6A09E667F3BCC909ULL;

	for (double& sample : input)
	{
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		const std::uint32_t highBits = static_cast<std::uint32_t>(state >> 32);
		sample = static_cast<double>(highBits) / 4294967295.0 - 0.5;
	}

	return input;
}

void initializeEngine(
	FilterEngine& engine,
	const std::wstring& configPath,
	unsigned channels,
	unsigned channelMask)
{
	const std::wstring deviceName = L"SubwooferRoutingEngineTests";
	const std::wstring connectionName = L"File";
	const std::wstring deviceGuid;
	const std::wstring deviceString =
		deviceName + L" " + connectionName + L" " + deviceGuid;

	engine.setDeviceInfo(
		false,
		true,
		deviceName,
		connectionName,
		deviceGuid,
		deviceString);
	engine.initialize(
		static_cast<float>(kSampleRate),
		channels,
		channels,
		channels,
		channelMask,
		kBlockFrames,
		configPath);
}

// The input is deliberately taken by value so every engine receives an
// independent writable buffer containing exactly the same samples.
std::vector<double> runEngine(
	const std::wstring& configPath,
	unsigned channels,
	unsigned channelMask,
	std::vector<double> input)
{
	harness.require(
		input.size() % (static_cast<size_t>(channels) * kBlockFrames) == 0,
		"engine input must contain complete 512-frame blocks");

	std::vector<double> output(input.size(), 0.0);
	const unsigned blockCount = static_cast<unsigned>(
		input.size() / (static_cast<size_t>(channels) * kBlockFrames));

	FilterEngine engine;
	initializeEngine(engine, configPath, channels, channelMask);

	for (unsigned block = 0; block < blockCount; ++block)
	{
		const size_t offset =
			static_cast<size_t>(block) * kBlockFrames * channels;
		engine.process(
			output.data() + offset,
			input.data() + offset,
			kBlockFrames);
	}

	return output;
}

struct DifferenceResult
{
	double maxAbsoluteDifference = 0.0;
	size_t sampleIndex = 0;
};

DifferenceResult findMaximumDifference(
	const std::vector<double>& left,
	const std::vector<double>& right)
{
	harness.requireEqual(
		left.size(),
		right.size(),
		"buffers being compared must have equal sizes");

	DifferenceResult result;
	for (size_t index = 0; index < left.size(); ++index)
	{
		const double difference = std::fabs(left[index] - right[index]);
		if (!std::isfinite(difference))
		{
			result.maxAbsoluteDifference =
				std::numeric_limits<double>::infinity();
			result.sampleIndex = index;
			return result;
		}

		if (difference > result.maxAbsoluteDifference)
		{
			result.maxAbsoluteDifference = difference;
			result.sampleIndex = index;
		}
	}

	return result;
}

std::string makeDifferenceMessage(
	const char* description,
	const DifferenceResult& difference,
	unsigned channels,
	double tolerance)
{
	char message[320];
	const size_t frame = difference.sampleIndex / channels;
	const size_t channel = difference.sampleIndex % channels;
	std::snprintf(
		message,
		sizeof(message),
		"%s: max absolute difference %.17g at sample %zu "
		"(frame %zu, channel %zu), tolerance %.17g",
		description,
		difference.maxAbsoluteDifference,
		difference.sampleIndex,
		frame,
		channel,
		tolerance);
	return message;
}

class SuiteFixture
{
public:
	SuiteFixture()
	{
		createDirectory();
		createFiles();
	}

	~SuiteFixture()
	{
		DeleteFileW(invalidStatePath_.c_str());
		DeleteFileW(profileConfigPath_.c_str());
		DeleteFileW(profileStatePath_.c_str());
		DeleteFileW(stateConfigPath_.c_str());
		DeleteFileW(originalConfigPath_.c_str());
		RemoveDirectoryW(directory_.c_str());
	}

	const std::wstring& originalConfigPath() const
	{
		return originalConfigPath_;
	}

	const std::wstring& stateConfigPath() const
	{
		return stateConfigPath_;
	}

	const std::wstring& profileConfigPath() const
	{
		return profileConfigPath_;
	}

	const std::wstring& invalidStatePath() const
	{
		return invalidStatePath_;
	}

private:
	void createDirectory()
	{
		wchar_t temporaryPath[MAX_PATH];
		const DWORD pathLength = GetTempPathW(MAX_PATH, temporaryPath);
		harness.require(
			pathLength > 0 && pathLength < MAX_PATH,
			"GetTempPathW must return a usable temporary directory");

		std::wstring root(temporaryPath, pathLength);
		if (!root.empty() && root.back() != L'\\' && root.back() != L'/')
			root += L'\\';

		const std::wstring prefix =
			root +
			L"bm_engine_" +
			std::to_wstring(GetCurrentProcessId()) +
			L"_" +
			std::to_wstring(GetTickCount64());

		for (unsigned attempt = 0; attempt < 100; ++attempt)
		{
			const std::wstring candidate =
				prefix + L"_" + std::to_wstring(attempt);
			if (CreateDirectoryW(candidate.c_str(), nullptr))
			{
				directory_ = candidate;
				break;
			}

			if (GetLastError() != ERROR_ALREADY_EXISTS)
				break;
		}

		harness.require(
			!directory_.empty(),
			"could not create a unique Subwoofer Routing engine test directory");

		originalConfigPath_ = directory_ + L"\\original.txt";
		stateConfigPath_ = directory_ + L"\\bm_state.txt";
		profileStatePath_ = directory_ + L"\\preset.swxt.json";
		profileConfigPath_ = directory_ + L"\\bm_profile.txt";
		invalidStatePath_ = directory_ + L"\\invalid_state.txt";
	}

	void createFiles()
	{
		subroute::PresetCreateResult preset =
			subroute::createBuiltInPreset(
				subroute::kIssue246FrontRear41PresetId);
		harness.require(
			preset.succeeded() && preset.state.has_value(),
			"the built-in issue-246-front-rear-4.1 preset must be available");

		subroute::SubwooferRoutingState state = *preset.state;
		state.headroom.mode = subroute::HeadroomMode::Manual;
		state.headroom.manualTrimDb = 0.0;

		subroute::StateEncodeResult encoded =
			subroute::encodeStateCanonical(state);
		harness.require(
			encoded.succeeded() && encoded.text.has_value(),
			"the issue #246 preset must encode canonically");

		const std::string& json = *encoded.text;
		harness.require(
			isPureAscii(json),
			"canonical Subwoofer Routing state JSON must be pure ASCII");

		harness.require(
			writeUtf8File(originalConfigPath_, kOriginalConfig),
			"could not write original.txt");
		harness.require(
			writeUtf8File(
				stateConfigPath_,
				std::string("SubwooferRouting: State ") + json + "\n"),
			"could not write bm_state.txt");
		harness.require(
			writeUtf8File(profileStatePath_, json),
			"could not write preset.swxt.json");
		harness.require(
			writeUtf8File(
				profileConfigPath_,
				"SubwooferRouting: Profile preset.swxt.json\n"),
			"could not write bm_profile.txt");
		harness.require(
			writeUtf8File(
				invalidStatePath_,
				"SubwooferRouting: State "
				"{\"schema\":\"wrong\",\"version\":1}\n"),
			"could not write invalid_state.txt");
	}

	std::wstring directory_;
	std::wstring originalConfigPath_;
	std::wstring stateConfigPath_;
	std::wstring profileStatePath_;
	std::wstring profileConfigPath_;
	std::wstring invalidStatePath_;
};

void testCommandRecognition()
{
	harness.expectTrue(
		FilterFactoryRegistry::canonicalCommand(L"SubwooferRouting")
			== std::wstring(L"SubwooferRouting"),
		"SubwooferRouting must resolve to its canonical engine command");

	const std::set<std::wstring>& commands =
		FilterFactoryRegistry::knownConfigCommands();
	harness.expectTrue(
		commands.find(L"SubwooferRouting") != commands.end(),
		"knownConfigCommands must contain SubwooferRouting");
}

std::vector<double> testOriginalChainParity(
	const SuiteFixture& fixture,
	const std::vector<double>& input)
{
	harness.requireEqual(
		input.size(),
		static_cast<size_t>(kNoiseFrames) * kFiveChannels,
		"parity input must contain 46080 five-channel frames");
	harness.requireEqual(
		kNoiseBlockCount,
		90U,
		"parity schedule must contain 90 complete blocks");

	const std::vector<double> originalOutput = runEngine(
		fixture.originalConfigPath(),
		kFiveChannels,
		kFiveChannelMask,
		input);
	std::vector<double> stateOutput = runEngine(
		fixture.stateConfigPath(),
		kFiveChannels,
		kFiveChannelMask,
		input);

	const DifferenceResult difference =
		findMaximumDifference(originalOutput, stateOutput);
	constexpr double tolerance = 1.0e-9;
	harness.expect(
		difference.maxAbsoluteDifference <= tolerance,
		makeDifferenceMessage(
			"built-in preset output differs from the original issue #246 chain",
			difference,
			kFiveChannels,
			tolerance));

	return stateOutput;
}

void testProfileFormEquivalence(
	const SuiteFixture& fixture,
	const std::vector<double>& input,
	const std::vector<double>& stateOutput)
{
	const std::vector<double> profileOutput = runEngine(
		fixture.profileConfigPath(),
		kFiveChannels,
		kFiveChannelMask,
		input);

	harness.requireEqual(
		profileOutput.size(),
		stateOutput.size(),
		"Profile-form and State-form outputs must have equal sizes");

	const bool identical =
		profileOutput.empty() ||
		std::memcmp(
			profileOutput.data(),
			stateOutput.data(),
			profileOutput.size() * sizeof(double)) == 0;

	std::string message =
		"Profile-form output must be bit-identical to State-form output";
	if (!identical)
	{
		size_t firstDifference = 0;
		while (
			firstDifference < profileOutput.size() &&
			bitEqual(
				profileOutput[firstDifference],
				stateOutput[firstDifference]))
		{
			++firstDifference;
		}

		const DifferenceResult difference =
			findMaximumDifference(profileOutput, stateOutput);
		char details[320];
		std::snprintf(
			details,
			sizeof(details),
			"Profile-form output must be bit-identical to State-form output; "
			"first differing sample %zu, max absolute difference %.17g",
			firstDifference,
			difference.maxAbsoluteDifference);
		message = details;
	}

	harness.expect(identical, message);
}

bool findFirstNonzeroMagnitude(
	const std::vector<double>& output,
	unsigned channels,
	unsigned channel,
	double& magnitude,
	size_t& frame)
{
	const size_t frameCount = output.size() / channels;
	for (size_t currentFrame = 0; currentFrame < frameCount; ++currentFrame)
	{
		const double sample =
			output[currentFrame * channels + channel];
		if (sample != 0.0)
		{
			magnitude = std::fabs(sample);
			frame = currentFrame;
			return true;
		}
	}

	return false;
}

void testSourceLfePreservation(const SuiteFixture& fixture)
{
	// Ninety-four complete blocks cover 48128 frames. The assertions inspect
	// at least the first 48000 frames, so the requested full second is covered
	// without issuing a short final process() call.
	constexpr unsigned processedFrames = 94 * kBlockFrames;
	constexpr unsigned oneSecondFrames = kSampleRate;
	std::vector<double> input(
		static_cast<size_t>(processedFrames) * kFiveChannels,
		0.0);
	input[2] = 1.0;

	const std::vector<double> output = runEngine(
		fixture.stateConfigPath(),
		kFiveChannels,
		kFiveChannelMask,
		input);

	constexpr double sourceLfeToMain = 3.1622776601683795;
	constexpr double sourceLfeToLfe = 0.6309573444801932;
	constexpr double tolerance = 1.0e-9;

	double leftMagnitude = 0.0;
	double rightMagnitude = 0.0;
	double lfeMagnitude = 0.0;
	size_t leftFrame = 0;
	size_t rightFrame = 0;
	size_t lfeFrame = 0;

	const bool foundLeft = findFirstNonzeroMagnitude(
		output,
		kFiveChannels,
		0,
		leftMagnitude,
		leftFrame);
	const bool foundRight = findFirstNonzeroMagnitude(
		output,
		kFiveChannels,
		1,
		rightMagnitude,
		rightFrame);
	const bool foundLfe = findFirstNonzeroMagnitude(
		output,
		kFiveChannels,
		2,
		lfeMagnitude,
		lfeFrame);

	harness.require(
		foundLeft,
		"an LFE-only impulse must reach the L output");
	harness.require(
		foundRight,
		"an LFE-only impulse must reach the R output");
	harness.require(
		foundLfe,
		"an LFE-only impulse must reach the LFE output");

	harness.expect(
		std::fabs(leftMagnitude - sourceLfeToMain) <= tolerance,
		"L output must preserve the SourceLFE +10 dB pure-gain impulse");
	harness.expect(
		std::fabs(rightMagnitude - sourceLfeToMain) <= tolerance,
		"R output must preserve the SourceLFE +10 dB pure-gain impulse");
	harness.expect(
		std::fabs(lfeMagnitude - sourceLfeToLfe) <= tolerance,
		"LFE output must preserve the SourceLFE net -4 dB impulse");
	harness.expectEqual(
		leftFrame,
		static_cast<size_t>(0),
		"the SourceLFE path to L must have no latency");
	harness.expectEqual(
		rightFrame,
		static_cast<size_t>(0),
		"the SourceLFE path to R must have no latency");
	harness.expectEqual(
		lfeFrame,
		static_cast<size_t>(0),
		"the SourceLFE path to LFE must have no latency");

	bool rearChannelsAreZero = true;
	for (unsigned frame = 0; frame < oneSecondFrames; ++frame)
	{
		const size_t offset = static_cast<size_t>(frame) * kFiveChannels;
		if (output[offset + 3] != 0.0 || output[offset + 4] != 0.0)
		{
			rearChannelsAreZero = false;
			break;
		}
	}

	harness.expectTrue(
		rearChannelsAreZero,
		"RL and RR must remain exactly zero for the full second");
}

void testInvalidStateDoesNotMute(const SuiteFixture& fixture)
{
	constexpr unsigned blockCount = 4;
	constexpr unsigned frames = blockCount * kBlockFrames;

	FilterEngine engine;
	initializeEngine(
		engine,
		fixture.invalidStatePath(),
		kFiveChannels,
		kFiveChannelMask);

	const bool loaded = engine.loadConfig(fixture.invalidStatePath());
	harness.expectTrue(
		loaded,
		"an invalid SubwooferRouting state line must not fail the overall engine config load");

	// A reload can publish through the engine's normal transition channel.
	// Drive one complete block before the measured run so the 480-sample
	// transition has completed. There are no filters on either side.
	std::vector<double> warmInput =
		makeNoise(kFiveChannels, kBlockFrames);
	std::vector<double> warmOutput(warmInput.size(), 0.0);
	engine.process(
		warmOutput.data(),
		warmInput.data(),
		kBlockFrames);

	std::vector<double> input = makeNoise(kFiveChannels, frames);
	const std::vector<double> expected = input;
	std::vector<double> output(input.size(), 0.0);

	for (unsigned block = 0; block < blockCount; ++block)
	{
		const size_t offset =
			static_cast<size_t>(block) * kBlockFrames * kFiveChannels;
		engine.process(
			output.data() + offset,
			input.data() + offset,
			kBlockFrames);
	}

	const bool identical =
		output.empty() ||
		std::memcmp(
			output.data(),
			expected.data(),
			output.size() * sizeof(double)) == 0;
	harness.expectTrue(
		identical,
		"an invalid SubwooferRouting state must leave audio bit-exactly unchanged");
}

void testNonParticipatingChannelPassThrough(
	const SuiteFixture& fixture)
{
	constexpr unsigned blockCount = 8;
	constexpr unsigned frames = blockCount * kBlockFrames;
	constexpr unsigned centerChannelIndex = 2;

	const std::vector<double> input =
		makeNoise(kSixChannels, frames);
	const std::vector<double> output = runEngine(
		fixture.stateConfigPath(),
		kSixChannels,
		kSixChannelMask,
		input);

	bool centerIsBitExact = true;
	bool participatingChannelChanged = false;

	for (unsigned frame = 0; frame < frames; ++frame)
	{
		const size_t offset = static_cast<size_t>(frame) * kSixChannels;

		if (!bitEqual(
			output[offset + centerChannelIndex],
			input[offset + centerChannelIndex]))
		{
			centerIsBitExact = false;
		}

		for (unsigned channel = 0; channel < kSixChannels; ++channel)
		{
			if (
				channel != centerChannelIndex &&
				!bitEqual(
					output[offset + channel],
					input[offset + channel]))
			{
				participatingChannelChanged = true;
			}
		}
	}

	harness.expectTrue(
		centerIsBitExact,
		"the unreferenced C channel must pass through bit-exactly");
	harness.expectTrue(
		participatingChannelChanged,
		"Subwoofer Routing must remain active when the device has an extra C channel");
}

}

void runSubwooferRoutingEngineTests();

void runSubwooferRoutingEngineTests()
{
	{
		SuiteFixture fixture;

		testCommandRecognition();

		const std::vector<double> input =
			makeNoise(kFiveChannels, kNoiseFrames);
		const std::vector<double> stateOutput =
			testOriginalChainParity(fixture, input);

		testProfileFormEquivalence(
			fixture,
			input,
			stateOutput);
		testSourceLfePreservation(fixture);
		testInvalidStateDoesNotMute(fixture);
		testNonParticipatingChannelPassThrough(fixture);
	}

	harness.report();
}
