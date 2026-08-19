/*
	This file is part of EqualizerAPO-XT.

	The installer window's state model: step transitions, byte formatting and
	channel descriptions. The window itself is Win32/Direct2D and stays
	untested, but every string and state it renders comes from this unit.
*/

#include <string>

#include "Installer/InstallerUiModel.h"

#include "EditorLogicTestSupport.h"

using namespace InstallerUi;

namespace
{
QString wide(const std::wstring& text)
{
	return QString::fromStdWString(text);
}
}

void testInstallerUiModelStepTransitions()
{
	Model model;
	for (int i = 0; i < kStepCount; i++)
		expectTrue(model.states[i] == StepState::Pending, "steps start pending");

	startStep(model, kStepDetect, L"detecting");
	expectTrue(model.states[kStepDetect] == StepState::Active, "detect becomes active");
	expectEqual(wide(model.details[kStepDetect]), QStringLiteral("detecting"), "detail is stored");

	// Starting a later step retires every earlier non-failed step, so a
	// skipped intermediate state can never leave a stale spinner behind.
	startStep(model, kStepVerify, L"verifying");
	expectTrue(model.states[kStepDetect] == StepState::Done, "earlier active step is retired");
	expectTrue(model.states[kStepDownload] == StepState::Done, "skipped step is retired");
	expectTrue(model.states[kStepVerify] == StepState::Active, "verify becomes active");
	expectTrue(model.states[kStepLaunch] == StepState::Pending, "later step stays pending");

	finishStep(model, kStepVerify, L"ok");
	expectTrue(model.states[kStepVerify] == StepState::Done, "finish marks done");
	expectEqual(wide(model.details[kStepVerify]), QStringLiteral("ok"), "finish replaces the detail");

	startStep(model, kStepLaunch, L"launching");
	failStep(model, kStepLaunch, L"boom");
	expectTrue(model.states[kStepLaunch] == StepState::Failed, "fail marks failed");
	expectEqual(wide(model.errorText), QStringLiteral("boom"), "fail records the error panel text");
	expectTrue(model.details[kStepLaunch].empty(),
		"fail clears the in-progress detail so it cannot read as the error");

	// A failed step is never retroactively marked done by a later start.
	startStep(model, kStepLaunch, L"");
	Model failedEarlier;
	failStep(failedEarlier, kStepDownload, L"down");
	startStep(failedEarlier, kStepLaunch, L"");
	expectTrue(failedEarlier.states[kStepDownload] == StepState::Failed,
		"a failed step survives a later start");

	// Out-of-range indices are ignored instead of writing out of bounds.
	Model bounds;
	startStep(bounds, -1, L"x");
	startStep(bounds, kStepCount, L"x");
	finishStep(bounds, kStepCount, L"x");
	failStep(bounds, -1, L"x");
	for (int i = 0; i < kStepCount; i++)
		expectTrue(bounds.states[i] == StepState::Pending, "out-of-range transitions are ignored");
	expectTrue(bounds.errorText.empty(), "out-of-range fail records nothing");
}

void testInstallerUiModelFormatting()
{
	expectEqual(wide(formatByteSize(512)), QStringLiteral("512 B"), "bytes render as B");
	expectEqual(wide(formatByteSize(1536)), QStringLiteral("1.5 KB"), "KB with one decimal");
	expectEqual(wide(formatByteSize(276824064)), QStringLiteral("264.0 MB"), "MB with one decimal");
	expectEqual(wide(formatByteSize(1610612736)), QStringLiteral("1.50 GB"), "GB with two decimals");

	expectEqual(wide(formatDownloadDetail(116686848, 276824064)),
		QStringLiteral("111.3 MB / 264.0 MB"), "known total renders as x / y");
	expectEqual(wide(formatDownloadDetail(116686848, 0)),
		QStringLiteral("111.3 MB"), "unknown total renders the running count only");

	expectTrue(downloadFraction(0, 0) < 0.0, "no total means indeterminate");
	expectTrue(downloadFraction(50, 200) == 0.25, "fraction is bytes over total");
	expectTrue(downloadFraction(300, 200) == 1.0, "fraction is clamped to 1");

	const std::wstring hash = L"91fb5d115b20f8d78fb8e3ff4f0e81334122b9819fda29923d0e4c9be27e9757";
	expectEqual(wide(shortHash(hash)), QStringLiteral("91fb5d115b20\u2026"),
		"short hash keeps 12 digits plus an ellipsis");
	expectEqual(wide(shortHash(L"abcdef")), QStringLiteral("abcdef"),
		"short input passes through unchanged");
}

void testInstallerUiModelChannelDescriptions()
{
	// Every channel in the release grammar has a human description; an
	// unknown channel falls through verbatim so it never renders empty.
	expectEqual(wide(describeChannel(L"x64-sse2")), QStringLiteral("64-bit x86 with SSE2"), "sse2");
	expectEqual(wide(describeChannel(L"x64-avx")), QStringLiteral("64-bit x86 with AVX"), "avx");
	expectEqual(wide(describeChannel(L"x64-avx2")), QStringLiteral("64-bit x86 with AVX2"), "avx2");
	expectEqual(wide(describeChannel(L"x64-avx512")), QStringLiteral("64-bit x86 with AVX-512"), "avx512");
	expectEqual(wide(describeChannel(L"x64-avx10-1")), QStringLiteral("64-bit x86 with AVX10.1"), "avx10.1");
	expectEqual(wide(describeChannel(L"arm64-neon")), QStringLiteral("ARM64 with NEON"), "arm64");
	expectEqual(wide(describeChannel(L"riscv-rvv")), QStringLiteral("riscv-rvv"), "unknown passes through");
}
