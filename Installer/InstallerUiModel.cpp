/*
	This file is part of EqualizerAPO-XT.

	Kept intentionally free of Win32/Direct2D types: EditorLogicTests
	compiles this file directly (see InstallerUiModel.h).
*/

#include "InstallerUiModel.h"

#include <cstdio>

namespace InstallerUi
{
const wchar_t* stepTitle(int step)
{
	switch (step)
	{
	case kStepDetect: return L"Select build";
	case kStepDownload: return L"Download";
	case kStepVerify: return L"Verify integrity";
	case kStepLaunch: return L"Start installer";
	default: return L"";
	}
}

void startStep(Model& model, int step, std::wstring detail)
{
	if (step < 0 || step >= kStepCount)
		return;
	for (int i = 0; i < step; i++)
	{
		if (model.states[i] != StepState::Failed && model.states[i] != StepState::Done)
			model.states[i] = StepState::Done;
	}
	model.states[step] = StepState::Active;
	model.details[step] = std::move(detail);
}

void finishStep(Model& model, int step, std::wstring detail)
{
	if (step < 0 || step >= kStepCount)
		return;
	model.states[step] = StepState::Done;
	model.details[step] = std::move(detail);
}

void failStep(Model& model, int step, std::wstring error)
{
	if (step < 0 || step >= kStepCount)
		return;
	model.states[step] = StepState::Failed;
	// The in-progress detail would otherwise linger under the failed title in
	// red, reading like part of the error. The error panel carries the message.
	model.details[step].clear();
	model.errorText = std::move(error);
}

std::wstring describeChannel(const std::wstring& channel)
{
	if (channel == L"x64-sse2")
		return L"64-bit x86 with SSE2";
	if (channel == L"x64-avx")
		return L"64-bit x86 with AVX";
	if (channel == L"x64-avx2")
		return L"64-bit x86 with AVX2";
	if (channel == L"x64-avx512")
		return L"64-bit x86 with AVX-512";
	if (channel == L"x64-avx10-1")
		return L"64-bit x86 with AVX10.1";
	if (channel == L"arm64-neon")
		return L"ARM64 with NEON";
	return channel;
}

std::wstring formatByteSize(unsigned long long bytes)
{
	wchar_t buffer[32] = {};
	if (bytes < 1024ULL)
		swprintf(buffer, 32, L"%llu B", bytes);
	else if (bytes < 1024ULL * 1024ULL)
		swprintf(buffer, 32, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
	else if (bytes < 1024ULL * 1024ULL * 1024ULL)
		swprintf(buffer, 32, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
	else
		swprintf(buffer, 32, L"%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
	return buffer;
}

std::wstring formatDownloadDetail(unsigned long long downloadedBytes,
	unsigned long long totalBytes)
{
	if (totalBytes == 0)
		return formatByteSize(downloadedBytes);
	return formatByteSize(downloadedBytes) + L" / " + formatByteSize(totalBytes);
}

double downloadFraction(unsigned long long downloadedBytes, unsigned long long totalBytes)
{
	if (totalBytes == 0)
		return -1.0;
	if (downloadedBytes >= totalBytes)
		return 1.0;
	return static_cast<double>(downloadedBytes) / static_cast<double>(totalBytes);
}

std::wstring shortHash(const std::wstring& hexHash)
{
	if (hexHash.length() <= 12)
		return hexHash;
	return hexHash.substr(0, 12) + L"\u2026";
}
}
