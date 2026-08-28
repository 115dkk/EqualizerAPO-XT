/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The test-side control interface of the fake ASIO driver. A probe or test
	QueryInterfaces the driver for it to configure the "hardware", drive
	buffer switches deterministically on its own thread, read back what
	reached the outputs, and inject the notifications real drivers send
	(reset requests, sample-rate changes).
*/

#pragma once

#include "asio/AsioSdk.h"

// {B7E3A9F4-52C1-4D0B-8A6E-1F9C3D5E7B21}: the driver's CLSID, which is also
// the IID an ASIO host asks for.
inline constexpr GUID CLSID_FakeAsio = {0xb7e3a9f4, 0x52c1, 0x4d0b, {0x8a, 0x6e, 0x1f, 0x9c, 0x3d, 0x5e, 0x7b, 0x21}};
// {A1F0C7D2-3B6E-4F1A-9C58-6E2D7B0A4F11}
inline constexpr GUID IID_IFakeAsioControl = {0xa1f0c7d2, 0x3b6e, 0x4f1a, {0x9c, 0x58, 0x6e, 0x2d, 0x7b, 0x0a, 0x4f, 0x11}};

struct FakeAsioConfig
{
	double sampleRate = 48000.0;
	long preferredSize = 64;
	long minSize = 8;
	long maxSize = 1024;
	long granularity = -1;          // powers of two
	long inputChannels = 2;
	long outputChannels = 2;
	long sampleType = ASIOSTInt32LSB;
	unsigned seed = 1;              // input generator seed
	long inputLatency = 0;
	long outputLatency = 0;
	long failInit = 0;              // 1 = init() answers ASIOFalse
};

struct FakeAsioCounters
{
	unsigned long initCalls = 0;
	unsigned long createBuffersCalls = 0;
	unsigned long disposeBuffersCalls = 0;
	unsigned long startCalls = 0;
	unsigned long stopCalls = 0;
	unsigned long outputReadyCalls = 0;
	unsigned long periods = 0;
	unsigned long timeInfoSwitches = 0;     // switches delivered through bufferSwitchTimeInfo
	long lastResetRequestAnswer = 0;        // what the host answered to kAsioResetRequest
	long hostSupportsTimeInfo = 0;
};

interface IFakeAsioControl : public IUnknown
{
	// Before init(). Replaces the whole configuration.
	virtual HRESULT STDMETHODCALLTYPE configure(const FakeAsioConfig* config) = 0;
	// After start(). Runs `periods` buffer switches synchronously on the
	// calling thread: fills the input half with the generator, calls the
	// host, then records the output half (at outputReady() if the host
	// called it, otherwise when the callback returns).
	virtual HRESULT STDMETHODCALLTYPE pump(long periods) = 0;
	// The bytes that reached one output channel since the last clear, in the
	// driver's sample format. The pointer stays valid until the next pump or
	// clear.
	virtual HRESULT STDMETHODCALLTYPE capturedOutput(long channel, const unsigned char** data, unsigned long* bytes) = 0;
	// The bytes the generator fed into one input channel since the last clear.
	virtual HRESULT STDMETHODCALLTYPE suppliedInput(long channel, const unsigned char** data, unsigned long* bytes) = 0;
	virtual HRESULT STDMETHODCALLTYPE clearRecords() = 0;
	// Sends kAsioResetRequest to the host through the callbacks it gave us.
	virtual HRESULT STDMETHODCALLTYPE raiseResetRequest() = 0;
	// Changes the rate and sends sampleRateDidChange.
	virtual HRESULT STDMETHODCALLTYPE raiseSampleRateChange(double rate) = 0;
	virtual HRESULT STDMETHODCALLTYPE counters(FakeAsioCounters* out) = 0;
};
