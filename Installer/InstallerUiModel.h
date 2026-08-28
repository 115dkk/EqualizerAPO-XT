/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The installer window's state model and its text formatting, kept free of
	any Win32 or Direct2D types so EditorLogicTests can compile it directly
	(the same pattern as AutoInstallerLogic). InstallerWindow renders this
	model; the worker thread in AutoInstaller.cpp mutates it through the
	transition helpers below.
*/

#pragma once

#include <array>
#include <string>

namespace InstallerUi
{
// The four stages the installer walks through, in display order.
enum StepIndex
{
	kStepDetect = 0,
	kStepDownload = 1,
	kStepVerify = 2,
	kStepLaunch = 3,
	kStepCount = 4
};

enum class StepState
{
	Pending,
	Active,
	Done,
	Failed
};

// Everything the window draws. Mutated only through the helpers below so the
// step-state invariants (one active step, failure stops the walk) hold.
struct Model
{
	std::array<StepState, kStepCount> states =
	{ StepState::Pending, StepState::Pending, StepState::Pending, StepState::Pending };
	// Secondary line under each step title; empty lines are not drawn.
	std::array<std::wstring, kStepCount> details;
	// Download progress. totalBytes == 0 means the size is unknown and the
	// bar renders as an indeterminate marquee.
	unsigned long long downloadedBytes = 0;
	unsigned long long totalBytes = 0;
	// Non-empty text switches the footer to the error panel with its actions.
	std::wstring errorText;
	// The launch step finished; the window closes itself shortly after.
	bool completed = false;
};

// Static row titles; the details carry the per-state dynamics.
const wchar_t* stepTitle(int step);

// Marks the step active. Any earlier non-failed step is marked done so a
// skipped intermediate state cannot leave a stale spinner behind.
void startStep(Model& model, int step, std::wstring detail);

// Marks the step done with its result line.
void finishStep(Model& model, int step, std::wstring detail);

// Marks the step failed and records the error panel text. Later steps stay
// pending; the walk is over.
void failStep(Model& model, int step, std::wstring error);

// "x64-avx2" -> "64-bit x86 with AVX2" and so on. Unknown channels come back
// verbatim so a future channel never renders as an empty line.
std::wstring describeChannel(const std::wstring& channel);

// 1536 -> "1.5 KB", 276824064 -> "264.0 MB". Bytes below 1 KB render as "n B".
std::wstring formatByteSize(unsigned long long bytes);

// "38.2 MB / 264.0 MB" while the total is known, otherwise just the running
// count.
std::wstring formatDownloadDetail(unsigned long long downloadedBytes,
	unsigned long long totalBytes);

// 0..1 when the total is known, -1 when the bar should render indeterminate.
double downloadFraction(unsigned long long downloadedBytes, unsigned long long totalBytes);

// First 12 hex digits plus an ellipsis, the trust line the verify step shows.
std::wstring shortHash(const std::wstring& hexHash);
}
