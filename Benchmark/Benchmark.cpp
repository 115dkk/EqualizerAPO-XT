/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2012  Jonas Thedering

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

#include "stdafx.h"
#ifdef DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#define _USE_MATH_DEFINES
#include <cmath>
#include <string>
#include <vector>
#include <sndfile.h>
#include <tclap/CmdLine.h>

#include "../version.h"
#include "../FilterEngine.h"
#include "../helpers/LogHelper.h"
#include "../helpers/StringHelper.h"
#include "../helpers/PrecisionTimer.h"
#include "../helpers/MemoryHelper.h"
#include "../helpers/PerfProfile.h"

using std::cerr;
using std::cout;
using std::log10;
using std::max;
using std::min;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;

int main(int argc, char** argv)
{
	try
	{
		stringstream versionStream;
		versionStream << MAJOR << "." << MINOR;
		if (REVISION != 0)
			versionStream << "." << REVISION;
		TCLAP::CmdLine cmd("Benchmark generates a linear sine sweep or reads from the given input file. "
			"It then filters the waveform using the Equalizer APO filter configuration "
			"and finally writes to the given file or into the user's temp directory.", ' ', versionStream.str());

		TCLAP::SwitchArg noPauseArg("", "nopause", "Do not wait for key press at the end", cmd);
		TCLAP::SwitchArg verboseArg("v", "verbose", "Print trace and error messages to console instead of logfile", cmd);
		TCLAP::SwitchArg profileArg("", "profile", "Enable per-stage performance instrumentation (FilterEngine + filter breakdown)", cmd);
		TCLAP::ValueArg<unsigned> repeatArg("", "repeat", "Number of times to repeat the processing loop for averaging (Default: 1)", false, 1, "integer", cmd);
		TCLAP::ValueArg<string> dumpCsvArg("", "dump-batches", "Path to a CSV file to write per-batch timings to", false, "", "string", cmd);
		TCLAP::ValueArg<string> configPathArg("", "config-path", "Override config directory; bypasses the registry ConfigPath lookup so the run is portable across machines", false, "", "string", cmd);
		TCLAP::ValueArg<string> guidArg("", "guid", "Endpoint GUID to use when parsing configuration (Default: <empty>)", false, "", "string", cmd);
		TCLAP::ValueArg<string> connectionnameArg("", "connectionname", "Connection name to use when parsing configuration (Default: File output)", false, "File output", "string", cmd);
		TCLAP::ValueArg<string> devicenameArg("", "devicename", "Device name to use when parsing configuration (Default: Benchmark)", false, "Benchmark", "string", cmd);
		TCLAP::ValueArg<unsigned> batchsizeArg("", "batchsize", "Number of frames processed in one batch (Default: 65536)", false, 65536, "integer", cmd);
		TCLAP::ValueArg<string> outputArg("o", "output", "File to write sound data to", false, "", "string", cmd);
		TCLAP::ValueArg<string> inputArg("i", "input", "File to load sound data from instead of generating sweep", false, "", "string", cmd);
		TCLAP::ValueArg<unsigned> rateArg("r", "rate", "Sample rate of generated sweep (Default: 44100)", false, 44100, "integer", cmd);
		TCLAP::ValueArg<float> toArg("t", "to", "End frequency of generated sweep in Hz (Default: 20000.0)", false, 20000.0f, "float", cmd);
		TCLAP::ValueArg<float> fromArg("f", "from", "Start frequency of generated sweep in Hz (Default: 0.1)", false, 1.0f, "float", cmd);
		TCLAP::ValueArg<float> lengthArg("l", "length", "Length of generated sweep in seconds (Default: 200.0)", false, 200.0f, "float", cmd);
		TCLAP::ValueArg<unsigned> channelArg("c", "channels", "Number of channels of generated sweep (Default: 2)", false, 2, "integer", cmd);

		cmd.parse(argc, argv);

		bool verbose = verboseArg.getValue();
		LogHelper::set(stderr, verbose, true, true);
#ifdef _DEBUG
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		// _CrtSetBreakAlloc(3318);
#endif

		unsigned sampleRate;
		unsigned channelCount;
		unsigned channelMask;
		unsigned frameCount;
		float length;
		vector<float> buf;

		if (REVISION == 0)
			cout << "Benchmark " << MAJOR << "." << MINOR << "\n";
		else
			cout << "Benchmark " << MAJOR << "." << MINOR << "." << REVISION << "\n";

		cout << "Run \"" << argv[0] << " -h\" to show usage info\n\n";

		string input = inputArg.getValue();
		if (input != "")
		{
			cout << "Reading sound data from " << input << "\n";

			PrecisionTimer timer;
			timer.start();

			SF_INFO info;
			SNDFILE* inFile = sf_open(input.c_str(), SFM_READ, &info);
			if (inFile == nullptr)
			{
				cerr << sf_strerror(inFile);
				return 1;
			}

			sampleRate = info.samplerate;
			channelCount = info.channels;
			channelMask = 0;
			frameCount = (unsigned)info.frames;
			length = float(frameCount) / sampleRate;

			buf.resize((size_t)frameCount * channelCount);

			sf_count_t numRead = 0;
			while (numRead < frameCount)
				numRead += sf_readf_float(inFile, buf.data() + numRead * channelCount, frameCount - numRead);

			sf_close(inFile);
			inFile = nullptr;

			double readTime = timer.stop();
			cout << "Reading input file took " << readTime << " seconds\n";
		}
		else
		{
			sampleRate = rateArg.getValue();
			channelMask = 0;
			channelCount = channelArg.getValue();
			float sweepFrom = fromArg.getValue();
			float sweepTo = toArg.getValue();
			float sweepDiff = sweepTo - sweepFrom;
			length = lengthArg.getValue();
			frameCount = (unsigned)(length * sampleRate);

			cout << "No input file given, so generating linear sine sweep from " << sweepFrom << " to " << sweepTo << " Hz over " << length << " seconds\n";

			PrecisionTimer timer;
			timer.start();

			buf.resize((size_t)frameCount * channelCount);
			for (unsigned i = 0; i < frameCount; i++)
			{
				double t = i * 1.0 / sampleRate;
				float s = static_cast<float>(sin(((sweepFrom + sweepDiff * (t / length) / 2) * t) * 2 * M_PI));

				for (unsigned j = 0; j < channelCount; j++)
					buf[i * channelCount + j] = s;
			}

			double genTime = timer.stop();
			cout << "Generating sweep took " << genTime << " seconds\n";
		}

		unsigned batchsize = batchsizeArg.getValue();

		vector<float> buf2((size_t)frameCount * channelCount, 0.0f);

		PrecisionTimer timer;
		timer.start();
		{
			FilterEngine engine;
			wstring deviceName = StringHelper::toWString(devicenameArg.getValue(), CP_ACP);
			wstring connectionName = StringHelper::toWString(connectionnameArg.getValue(), CP_ACP);
			wstring deviceGuid = StringHelper::toWString(guidArg.getValue(), CP_ACP);
			engine.setDeviceInfo(false, true, deviceName, connectionName, deviceGuid, deviceName + L" " + connectionName + L" " + deviceGuid);
			wstring customConfigPath = StringHelper::toWString(configPathArg.getValue(), CP_ACP);
			if (!customConfigPath.empty())
			{
				DWORD attrs = GetFileAttributesW(customConfigPath.c_str());
				if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
					customConfigPath += L"\\config.txt";
			}
			engine.initialize(static_cast<float>(sampleRate), channelCount, channelCount, channelCount, channelMask, batchsize, customConfigPath);

			double initTime = timer.stop();
			if (!verbose)
				cout << "\nLoading configuration took " << initTime * 1000.0 << " ms\n";

			unsigned repeatCount = repeatArg.getValue();
			if (repeatCount < 1) repeatCount = 1;
			bool profileEnabled = profileArg.getValue();

			if (profileEnabled)
			{
				PerfProfile::reset();
				PerfProfile::enable();
			}

			cout << "\nProcessing " << frameCount << " frames from " << channelCount << " channel(s)";
			if (repeatCount > 1)
				cout << " x" << repeatCount << " repetitions";
			cout << "\n";

			vector<double> batchTimes;
			batchTimes.reserve(((size_t)frameCount / batchsize + 1) * repeatCount);

			timer.start();

			PrecisionTimer batchTimer;
			for (unsigned r = 0; r < repeatCount; r++)
			{
				for (unsigned i = 0; i < frameCount; i += batchsize)
				{
					unsigned actual = min(batchsize, frameCount - i);
					batchTimer.start();
					engine.process(buf2.data() + i * channelCount, buf.data() + i * channelCount, actual);
					batchTimes.push_back(batchTimer.stop());
				}
			}

			double time = timer.stop();

			if (profileEnabled)
				PerfProfile::disable();

			double totalAudio = length * repeatCount;

			cout << frameCount * channelCount * repeatCount << " samples processed in " << time << " seconds\n";
			cout << "This is equivalent to " << 100.0f * time / totalAudio << "% CPU load (one core) when processing in real time\n";

			if (!batchTimes.empty())
			{
				vector<double> sorted = batchTimes;
				std::sort(sorted.begin(), sorted.end());
				auto pick = [&](double pct) {
					size_t n = sorted.size();
					size_t idx = (size_t)(pct * (n - 1));
					return sorted[idx];
				};
				double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
				double mean = sum / sorted.size();
				cout << "Batch timings (n=" << sorted.size() << ", " << batchsize << " frames each, microseconds):\n";
				cout << "  min:    " << sorted.front() * 1e6 << "\n";
				cout << "  median: " << pick(0.50) * 1e6 << "\n";
				cout << "  mean:   " << mean * 1e6 << "\n";
				cout << "  p95:    " << pick(0.95) * 1e6 << "\n";
				cout << "  p99:    " << pick(0.99) * 1e6 << "\n";
				cout << "  max:    " << sorted.back() * 1e6 << "\n";
			}

			if (profileEnabled)
				PerfProfile::report(cout);

			string dumpCsvPath = dumpCsvArg.getValue();
			if (!dumpCsvPath.empty())
			{
				std::ofstream csv(dumpCsvPath);
				csv << "batch_index,seconds\n";
				for (size_t i = 0; i < batchTimes.size(); i++)
					csv << i << "," << batchTimes[i] << "\n";
				cout << "\nPer-batch timings written to " << dumpCsvPath << "\n";
			}

			unsigned clipCount = 0;
			float max = 0;
			for (unsigned i = 0; i < frameCount * channelCount; i++)
			{
				float f = fabs(buf2[i]);
				if (f > max)
					max = f;
				if (f > 1.0f)
					clipCount++;
			}

			cout << "Max output level: " << max << " (" << log10(max) * 20.0f << " dB)";
			if (clipCount > 0)
				cout << " (" << clipCount << " samples clipped!)";
			cout << "\n";

			string output = outputArg.getValue();
			if (output == "")
			{
				char temp[255];
				GetTempPathA(sizeof(temp) / sizeof(temp[0]), temp);

				output = temp;
				output += "testout.wav";
			}

			cout << "\nWriting output to " << output << "\n";

			SF_INFO info = {frameCount, static_cast<int>(sampleRate), static_cast<int>(channelCount), SF_FORMAT_WAV | SF_FORMAT_PCM_16, 0};
			SNDFILE* outFile = sf_open(output.c_str(), SFM_WRITE, &info);
			if (outFile == nullptr)
			{
				cerr << sf_strerror(outFile);
				return 1;
			}

			sf_count_t numWritten = 0;
			while (numWritten < frameCount)
				numWritten += sf_writef_float(outFile, buf2.data() + numWritten * channelCount, frameCount - numWritten);

			sf_close(outFile);
			outFile = nullptr;
		}

		if (!noPauseArg.getValue())
			std::system("pause");

		return 0;
	}
	catch (const TCLAP::ArgException& e)
	{
		cerr << "Error: " << e.error() << " for arg " << e.argId() << "\n";
		return -1;
	}
}
