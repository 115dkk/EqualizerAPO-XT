/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "asio/AsioWrapper.h"
#include "asio/InProcProcessor.h"
#include "engine/FilterEngine.h"
#include "platform/windows/ComPtr.h"
#include "Tests/AsioSupport/HostStub.h"
#include "Tests/AsioSupport/SampleOracle.h"
#include "Tests/FakeAsioDriver/FakeAsio.h"
#include "Tests/TestDirectory.h"
#include "Tests/TestHarness.h"

using eapo::asio::AsioWrapper;
using eapo::asio::StreamOptions;
using winutil::ComPtr;

namespace
{
	test::HarnessAccess harness;

	constexpr long frames = 64;
	constexpr long periods = 20;
	constexpr double sampleRate = 48000.0;
	constexpr unsigned fakeSeed = 11;
	constexpr unsigned hostSeed = 7;
	constexpr long channels = 2;
	constexpr GUID precisionWrapperClsid = {0x8e4b81cb, 0x51f4, 0x4f56, {0x91, 0xee, 0x30, 0xb7, 0xe5, 0xe9, 0x16, 0x3d}};

	struct WrappedResult
	{
		long hostType = 0;
		std::vector<std::vector<unsigned char>> capturedOutputs;
		std::vector<std::vector<unsigned char>> receivedInputs;
		std::vector<std::vector<unsigned char>> suppliedInputs;
	};

	std::vector<unsigned char> fakeRecord(IFakeAsioControl* control, long channel, bool input)
	{
		const unsigned char* data = nullptr;
		unsigned long byteCount = 0;
		const HRESULT result = input ? control->suppliedInput(channel, &data, &byteCount)
			: control->capturedOutput(channel, &data, &byteCount);
		harness.require(SUCCEEDED(result), input ? "read the fake input record" : "read the fake output record");
		return std::vector<unsigned char>(data, data + byteCount);
	}

	WrappedResult runWrapped(const std::wstring& configPath, long targetType)
	{
		FakeAsioConfig config;
		config.sampleRate = sampleRate;
		config.preferredSize = frames;
		config.minSize = frames;
		config.maxSize = frames;
		config.inputChannels = channels;
		config.outputChannels = channels;
		config.sampleType = targetType;
		config.seed = fakeSeed;
		config.fullPrecision = 1;

		FakeAsioDriver* fake = new FakeAsioDriver();
		harness.requireEqual(fake->configure(&config), S_OK, "configure the precision fake");
		StreamOptions options;
		options.configPath = configPath;
		options.processInput = true;
		options.processOutput = true;
		AsioWrapper* concrete = new AsioWrapper(fake, precisionWrapperClsid, CLSID_FakeAsioText,
			options, std::make_unique<eapo::asio::InProcProcessor>());
		ComPtr<IASIO> wrapper;
		*wrapper.put() = concrete;
		fake->Release();

		harness.requireEqual(wrapper->init(nullptr), ASIOTrue, "initialize the precision wrapper");
		ASIOChannelInfo channelInfo = {};
		channelInfo.channel = 0;
		channelInfo.isInput = ASIOFalse;
		harness.requireEqual(wrapper->getChannelInfo(&channelInfo), ASE_OK, "read the wrapper output sample type");

		WrappedResult result;
		result.hostType = channelInfo.type;
		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = hostSeed;
		hostOptions.sampleType = result.hostType;
		hostOptions.fullPrecision = true;
		{
			asiotest::HostStub host(hostOptions);
			host.openChannels(channels, channels);
			harness.requireEqual(host.createBuffers(wrapper.get(), frames), ASE_OK, "create precision stream buffers");
			harness.requireEqual(wrapper->start(), ASE_OK, "start the precision stream");
			harness.requireEqual(fake->pump(periods), S_OK, "pump the precision stream");
			harness.requireEqual(wrapper->stop(), ASE_OK, "stop the precision stream");
			harness.requireEqual(wrapper->disposeBuffers(), ASE_OK, "dispose the precision stream buffers");

			result.capturedOutputs.resize(channels);
			result.receivedInputs.resize(channels);
			result.suppliedInputs.resize(channels);
			for (long channel = 0; channel < channels; channel++)
			{
				result.capturedOutputs[static_cast<size_t>(channel)] = fakeRecord(fake, channel, false);
				result.receivedInputs[static_cast<size_t>(channel)] = host.inputRecord(static_cast<size_t>(channel));
				result.suppliedInputs[static_cast<size_t>(channel)] = fakeRecord(fake, channel, true);
			}
		}
		wrapper.reset();
		return result;
	}

	std::vector<std::vector<double>> engineInDouble(const std::wstring& configPath, bool capture,
		long channelCount, long frameCount, long periodCount, double rate,
		const std::function<double(long, uint64_t)>& source)
	{
		FilterEngine engine;
		EngineSetup setup;
		setup.sampleRate = static_cast<float>(rate);
		setup.inputChannelCount = static_cast<unsigned>(channelCount);
		setup.realChannelCount = static_cast<unsigned>(channelCount);
		setup.outputChannelCount = static_cast<unsigned>(channelCount);
		setup.channelMask = 0;
		setup.maxFrameCount = static_cast<unsigned>(frameCount);
		setup.customPath = configPath;
		setup.capture = capture;
		setup.deviceName = L"FakeAsio";
		setup.connectionName = L"ASIO";
		setup.deviceGuid = CLSID_FakeAsioText;
		engine.initialize(setup);

		std::vector<std::vector<double>> result(static_cast<size_t>(channelCount));
		for (std::vector<double>& channel : result)
			channel.reserve(static_cast<size_t>(frameCount) * periodCount);
		std::vector<double> interleaved(static_cast<size_t>(channelCount) * frameCount);
		for (long period = 0; period < periodCount; period++)
		{
			for (long frame = 0; frame < frameCount; frame++)
			{
				const uint64_t sampleIndex = static_cast<uint64_t>(period) * frameCount + frame;
				for (long channel = 0; channel < channelCount; channel++)
					interleaved[static_cast<size_t>(frame) * channelCount + channel] = source(channel, sampleIndex);
			}
			engine.process(interleaved.data(), interleaved.data(), static_cast<unsigned>(frameCount));
			for (long frame = 0; frame < frameCount; frame++)
			{
				for (long channel = 0; channel < channelCount; channel++)
					result[static_cast<size_t>(channel)].push_back(
						interleaved[static_cast<size_t>(frame) * channelCount + channel]);
			}
		}
		return result;
	}

	double quantized(long sampleType, double value)
	{
		std::array<unsigned char, 8> bytes = {};
		double result = 0.0;
		asiotest::oracle::encode(sampleType, &value, bytes.data(), 1);
		asiotest::oracle::decode(sampleType, bytes.data(), &result, 1);
		return result;
	}

	std::vector<std::vector<unsigned char>> encodeRecords(long sampleType,
		const std::vector<std::vector<double>>& values, bool float32Seam)
	{
		const size_t bytesPerSample = asiotest::oracle::bytesPerSample(sampleType);
		std::vector<std::vector<unsigned char>> records(values.size());
		for (size_t channel = 0; channel < values.size(); channel++)
		{
			std::vector<double> samples = values[channel];
			if (float32Seam)
			{
				for (double& sample : samples)
					sample = asiotest::oracle::throughFloat32(sample);
			}
			records[channel].resize(samples.size() * bytesPerSample);
			asiotest::oracle::encode(sampleType, samples.data(), records[channel].data(), samples.size());
		}
		return records;
	}

	std::vector<std::vector<unsigned char>> engineRecords(const std::wstring& configPath, bool capture,
		long resultType, const std::function<double(long, uint64_t)>& source, bool float32Output)
	{
		return encodeRecords(resultType,
			engineInDouble(configPath, capture, channels, frames, periods, sampleRate, source), float32Output);
	}

	bool recordsDiffer(const std::vector<std::vector<unsigned char>>& left,
		const std::vector<std::vector<unsigned char>>& right)
	{
		return left != right;
	}

	void expectBytesEqual(const std::vector<std::vector<unsigned char>>& actual,
		const std::vector<std::vector<unsigned char>>& expected, long sampleType, const std::string& label)
	{
		harness.requireEqual(actual.size(), expected.size(), label + " channel count");
		const size_t sampleBytes = asiotest::oracle::bytesPerSample(sampleType);
		harness.require(sampleBytes != 0, label + " has a supported sample type");
		for (size_t channel = 0; channel < actual.size(); channel++)
		{
			harness.requireEqual(actual[channel].size(), expected[channel].size(),
				label + " channel " + std::to_string(channel) + " byte count");
			bool equal = true;
			for (size_t offset = 0; offset < actual[channel].size(); offset += sampleBytes)
			{
				if (std::memcmp(actual[channel].data() + offset, expected[channel].data() + offset, sampleBytes) == 0)
					continue;
				double actualValue = 0.0;
				double expectedValue = 0.0;
				asiotest::oracle::decode(sampleType, actual[channel].data() + offset, &actualValue, 1);
				asiotest::oracle::decode(sampleType, expected[channel].data() + offset, &expectedValue, 1);
				char message[512] = {};
				std::snprintf(message, sizeof(message), "%s channel %zu sample %zu: expected %.17g, got %.17g",
					label.c_str(), channel, offset / sampleBytes, expectedValue, actualValue);
				harness.expect(false, message);
				equal = false;
				break;
			}
			if (equal)
				harness.expect(true, label + " channel " + std::to_string(channel) + " matches");
		}
	}

	void writeFilterConfig(const std::wstring& path)
	{
		std::ofstream file(path);
		file << "Preamp: -6.0206 dB\n"
			"Filter: ON PK Fc 1000 Hz Gain -3 dB Q 1.0\n"
			"Filter: ON HS Fc 8000 Hz Gain 2 dB\n"
			"Channel: R\n"
			"Filter: ON LP Fc 12000 Hz\n"
			"Delay: 3 samples\n"
			"Channel: L R\n"
			"Copy: L=0.9*L+0.1*R R=0.1*L+0.9*R\n";
	}

	void testEnginePrecision(long targetType, const std::wstring& fixtureName)
	{
		test::TestDirectory directory(fixtureName);
		const std::wstring configPath = directory.trackFile(L"config.txt");
		writeFilterConfig(configPath);
		const WrappedResult wrapped = runWrapped(configPath, targetType);

		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = hostSeed;
		hostOptions.sampleType = wrapped.hostType;
		hostOptions.fullPrecision = true;
		{
			asiotest::HostStub host(hostOptions);
			const auto hostSource = [&host, &wrapped](long channel, uint64_t sampleIndex) {
				return quantized(wrapped.hostType, host.outputValue(channel, sampleIndex));
			};
			const auto hostFloatSource = [&hostSource](long channel, uint64_t sampleIndex) {
				return asiotest::oracle::throughFloat32(hostSource(channel, sampleIndex));
			};
			const std::vector<std::vector<unsigned char>> expectedOutputs =
				engineRecords(configPath, false, targetType, hostSource, false);
			const std::vector<std::vector<unsigned char>> floatOutputs =
				engineRecords(configPath, false, targetType, hostFloatSource, true);

			const auto fakeSource = [targetType](long channel, uint64_t sampleIndex) {
				return quantized(targetType,
					FakeAsioDriver::generatorValue(fakeSeed, channel, sampleIndex, true));
			};
			const auto fakeFloatSource = [&fakeSource](long channel, uint64_t sampleIndex) {
				return asiotest::oracle::throughFloat32(fakeSource(channel, sampleIndex));
			};
			const std::vector<std::vector<unsigned char>> expectedInputs =
				engineRecords(configPath, true, wrapped.hostType, fakeSource, false);
			const std::vector<std::vector<unsigned char>> floatInputs =
				engineRecords(configPath, true, wrapped.hostType, fakeFloatSource, true);

			harness.expect(recordsDiffer(expectedOutputs, floatOutputs),
				"the output signal distinguishes a float32 seam from the double engine");
			harness.expect(recordsDiffer(expectedInputs, floatInputs),
				"the input signal distinguishes a float32 seam from the double engine");
			expectBytesEqual(wrapped.capturedOutputs, expectedOutputs, targetType,
				"filtered output matches the double engine");
			expectBytesEqual(wrapped.receivedInputs, expectedInputs, wrapped.hostType,
				"filtered input matches the double engine");
		}
		directory.removeAll();
	}

	void testIdlePrecision(long targetType, const std::wstring& fixtureName)
	{
		test::TestDirectory directory(fixtureName);
		const std::wstring configPath = directory.trackFile(L"config.txt");
		{
			std::ofstream file(configPath);
			file << "# No filters.\n";
		}
		const WrappedResult wrapped = runWrapped(configPath, targetType);
		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = hostSeed;
		hostOptions.sampleType = wrapped.hostType;
		hostOptions.fullPrecision = true;
		{
			asiotest::HostStub host(hostOptions);
			std::vector<std::vector<double>> outputValues(channels);
			for (long channel = 0; channel < channels; channel++)
			{
				std::vector<double>& values = outputValues[static_cast<size_t>(channel)];
				values.resize(static_cast<size_t>(frames) * periods);
				for (size_t sample = 0; sample < values.size(); sample++)
					values[sample] = host.outputValue(channel, sample);
			}
			const std::vector<std::vector<unsigned char>> expectedOutputs =
				encodeRecords(wrapped.hostType, outputValues, false);
			expectBytesEqual(wrapped.capturedOutputs, expectedOutputs, wrapped.hostType,
				"idle output keeps every source bit");
			expectBytesEqual(wrapped.receivedInputs, wrapped.suppliedInputs, wrapped.hostType,
				"idle input keeps every source bit");
		}
		directory.removeAll();
	}

	TEST_CASE(PrecisionTests, testFullPrecisionSignalsExceedFloat32)
	{
		asiotest::HostStub::Options options;
		options.outputSeed = hostSeed;
		options.sampleType = ASIOSTInt32LSB;
		options.fullPrecision = true;
		{
			asiotest::HostStub host(options);
			for (long channel = 0; channel < channels; channel++)
			{
				std::vector<double> values(4096);
				bool inRange = true;
				size_t beyondFloat = 0;
				for (size_t sample = 0; sample < values.size(); sample++)
				{
					values[sample] = host.outputValue(channel, sample);
					inRange = inRange && values[sample] >= -0.5 && values[sample] < 0.5;
					if (asiotest::oracle::throughFloat32(values[sample]) != values[sample])
						beyondFloat++;
				}
				std::vector<unsigned char> bytes(values.size() * 4);
				std::vector<double> quantizedValues(values.size());
				asiotest::oracle::encode(ASIOSTInt32LSB, values.data(), bytes.data(), values.size());
				asiotest::oracle::decode(ASIOSTInt32LSB, bytes.data(), quantizedValues.data(), quantizedValues.size());
				size_t quantizedBeyondFloat = 0;
				for (double value : quantizedValues)
					if (asiotest::oracle::throughFloat32(value) != value)
						quantizedBeyondFloat++;
				harness.expect(inRange, "host channel " + std::to_string(channel) + " stays in [-0.5, 0.5)");
				harness.expect(beyondFloat >= 4000, "host channel " + std::to_string(channel) + " exceeds float32 precision");
				harness.expect(quantizedBeyondFloat >= 3600,
					"host channel " + std::to_string(channel) + " keeps low bits after Int32 quantization");
			}
		}

		for (long channel = 0; channel < channels; channel++)
		{
			std::vector<double> values(4096);
			bool inRange = true;
			size_t beyondFloat = 0;
			for (size_t sample = 0; sample < values.size(); sample++)
			{
				values[sample] = FakeAsioDriver::generatorValue(fakeSeed, channel, sample, true);
				inRange = inRange && values[sample] >= -0.5 && values[sample] < 0.5;
				if (asiotest::oracle::throughFloat32(values[sample]) != values[sample])
					beyondFloat++;
			}
			std::vector<unsigned char> bytes(values.size() * 4);
			std::vector<double> quantizedValues(values.size());
			asiotest::oracle::encode(ASIOSTInt32LSB, values.data(), bytes.data(), values.size());
			asiotest::oracle::decode(ASIOSTInt32LSB, bytes.data(), quantizedValues.data(), quantizedValues.size());
			size_t quantizedBeyondFloat = 0;
			for (double value : quantizedValues)
				if (asiotest::oracle::throughFloat32(value) != value)
					quantizedBeyondFloat++;
			harness.expect(inRange, "fake channel " + std::to_string(channel) + " stays in [-0.5, 0.5)");
			harness.expect(beyondFloat >= 4000, "fake channel " + std::to_string(channel) + " exceeds float32 precision");
			harness.expect(quantizedBeyondFloat >= 3600,
				"fake channel " + std::to_string(channel) + " keeps low bits after Int32 quantization");
		}
	}

	TEST_CASE(PrecisionTests, testFloat64TargetMatchesTheEngineInDouble)
	{
		testEnginePrecision(ASIOSTFloat64LSB, L"PrecisionTests-Float64-Engine");
	}

	TEST_CASE(PrecisionTests, testInt32TargetMatchesTheEngineInDouble)
	{
		testEnginePrecision(ASIOSTInt32LSB, L"PrecisionTests-Int32-Engine");
	}

	TEST_CASE(PrecisionTests, testInt32TargetReceivesEveryBitThroughAnIdleEngine)
	{
		testIdlePrecision(ASIOSTInt32LSB, L"PrecisionTests-Int32-Idle");
	}

	TEST_CASE(PrecisionTests, testFloat64TargetReceivesEveryBitThroughAnIdleEngine)
	{
		testIdlePrecision(ASIOSTFloat64LSB, L"PrecisionTests-Float64-Idle");
	}
}
