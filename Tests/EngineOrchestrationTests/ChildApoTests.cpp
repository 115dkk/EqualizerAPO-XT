/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <array>
#include "EqualizerAPO/ChildApo.h"
#include "../TestHarness.h"

namespace
{
constexpr wchar_t childClsid[] = L"{63D3A890-A77C-42B2-8947-B93925A1A50B}";
constexpr GUID childGuid = {0x63d3a890, 0xa77c, 0x42b2, {0x89, 0x47, 0xb9, 0x39, 0x25, 0xa1, 0xa5, 0x0b}};
using AttachResult = ChildApo::AttachResult;

enum class Call { QueryRealtime, QueryConfiguration, Initialize, Format, Latency, Lock, Unlock, Process };

struct ChildState
{
	ULONG references = 1;
	unsigned additions = 0;
	unsigned releases = 0;
	unsigned destructions = 0;
	bool hasRealtime = true;
	bool hasConfiguration = true;
	HRESULT initializeResult = S_OK;
	HRESULT formatResult = S_OK;
	HRESULT latencyResult = S_OK;
	HRESULT lockResult = S_OK;
	HRESULT unlockResult = S_OK;
	std::array<Call, 32> calls = {};
	size_t callCount = 0;
	UINT32 dataSize = 0;
	BYTE* data = nullptr;
	IAudioMediaType* oppositeFormat = nullptr;
	IAudioMediaType* requestedFormat = nullptr;
	IAudioMediaType** supportedFormat = nullptr;
	HNSTIME* latency = nullptr;
	UINT32 inputCount = 0;
	UINT32 outputCount = 0;
	APO_CONNECTION_PROPERTY** inputs = nullptr;
	APO_CONNECTION_PROPERTY** outputs = nullptr;
	APO_CONNECTION_DESCRIPTOR** inputDescriptors = nullptr;
	APO_CONNECTION_DESCRIPTOR** outputDescriptors = nullptr;

	void record(Call call)
	{
		if (callCount < calls.size())
			calls[callCount] = call;
		++callCount;
	}
};

class FakeChild final : public IAudioProcessingObject, public IAudioProcessingObjectRT,
	public IAudioProcessingObjectConfiguration
{
public:
	explicit FakeChild(ChildState& state) : state(state) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		*object = nullptr;
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioProcessingObject))
			*object = static_cast<IAudioProcessingObject*>(this);
		else if (iid == __uuidof(IAudioProcessingObjectRT))
		{
			state.record(Call::QueryRealtime);
			if (state.hasRealtime)
				*object = static_cast<IAudioProcessingObjectRT*>(this);
		}
		else if (iid == __uuidof(IAudioProcessingObjectConfiguration))
		{
			state.record(Call::QueryConfiguration);
			if (state.hasConfiguration)
				*object = static_cast<IAudioProcessingObjectConfiguration*>(this);
		}
		if (*object == nullptr)
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		++state.additions;
		return ++state.references;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		++state.releases;
		const ULONG remaining = --state.references;
		if (remaining == 0)
			delete this;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE Reset() override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType*, IAudioMediaType*, IAudioMediaType**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32*) override { return E_NOTIMPL; }
	UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 frames) override { return frames; }
	UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 frames) override { return frames; }

	HRESULT STDMETHODCALLTYPE Initialize(UINT32 size, BYTE* data) override
	{
		state.record(Call::Initialize);
		state.dataSize = size;
		state.data = data;
		return state.initializeResult;
	}

	HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* opposite,
		IAudioMediaType* requested, IAudioMediaType** supported) override
	{
		state.record(Call::Format);
		state.oppositeFormat = opposite;
		state.requestedFormat = requested;
		state.supportedFormat = supported;
		return state.formatResult;
	}

	HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* latency) override
	{
		state.record(Call::Latency);
		state.latency = latency;
		*latency = 12345;
		return state.latencyResult;
	}

	HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 inputCount, APO_CONNECTION_DESCRIPTOR** inputs,
		UINT32 outputCount, APO_CONNECTION_DESCRIPTOR** outputs) override
	{
		state.record(Call::Lock);
		state.inputCount = inputCount;
		state.outputCount = outputCount;
		state.inputDescriptors = inputs;
		state.outputDescriptors = outputs;
		return state.lockResult;
	}

	HRESULT STDMETHODCALLTYPE UnlockForProcess() override
	{
		state.record(Call::Unlock);
		return state.unlockResult;
	}

	void STDMETHODCALLTYPE APOProcess(UINT32 inputCount, APO_CONNECTION_PROPERTY** inputs,
		UINT32 outputCount, APO_CONNECTION_PROPERTY** outputs) override
	{
		state.record(Call::Process);
		state.inputCount = inputCount;
		state.outputCount = outputCount;
		state.inputs = inputs;
		state.outputs = outputs;
	}

private:
	~FakeChild() { ++state.destructions; }
	ChildState& state;
};

// Only pointer identity matters for forwarding. No format creation API or COM
// activation is needed, and the child must not acquire a reference to these.
class FakeMediaType final : public IAudioMediaType
{
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { *object = nullptr; return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }
	HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType*, DWORD*) override { return E_NOTIMPL; }
	const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() override { return nullptr; }
	HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT*) override { return E_NOTIMPL; }
};

struct Fixture
{
	ChildState state;
	winutil::ComPtr<FakeChild> owner;
	ChildApo child;
	unsigned activations = 0;
	HRESULT activationResult = S_OK;

	Fixture() { *owner.put() = new FakeChild(state); }

	~Fixture()
	{
		child.reset();
		test::activeHarness().expectEqual(state.references, 1ul, "reset leaves exactly the fixture's reference");
		test::activeHarness().expectEqual(state.destructions, 0u, "the child did not over-release the fixture's reference");
		owner.reset();
		test::activeHarness().expectEqual(state.references, 0ul, "releasing the fixture balances all references");
		test::activeHarness().expectEqual(state.destructions, 1u, "the fake is destroyed exactly once");
		test::activeHarness().expectEqual(state.releases, state.additions + 1, "every acquired reference was released exactly once");
	}

	ChildApo::Activator activator()
	{
		return [this](REFCLSID clsid, IAudioProcessingObject** object) {
			++activations;
			test::activeHarness().expect(clsid == childGuid, "the activator receives the parsed CLSID");
			*object = nullptr;
			if (FAILED(activationResult))
				return activationResult;
			*object = owner.get();
			owner->AddRef();
			return S_OK;
		};
	}

	AttachResult attach() { return child.attach(childClsid, 0, nullptr, activator()); }
};

TEST_CASE(ChildApoTests, testChildEmptyClsid)
{
	Fixture f;
	harness.expect(f.child.attach(L"", 0, nullptr, f.activator()) == AttachResult::NoChild, "empty CLSID means no child");
	harness.expectEqual(f.activations, 0u, "empty CLSID never activates COM");
	harness.expectFalse(f.child.attached(), "empty CLSID leaves the child detached");
}

TEST_CASE(ChildApoTests, testChildMalformedClsid)
{
	Fixture f;
	harness.expect(f.child.attach(L"{not-a-guid}", 0, nullptr, f.activator()) == AttachResult::BadClsid, "malformed CLSID is rejected");
	harness.expectEqual(f.activations, 0u, "malformed CLSID never calls the activator");
	harness.expectFalse(f.child.attached(), "malformed CLSID leaves the child detached");
}

TEST_CASE(ChildApoTests, testChildActivationFailure)
{
	Fixture f;
	f.activationResult = E_FAIL;
	harness.expect(f.attach() == AttachResult::ActivationFailed, "activation failure is reported");
	harness.expectFalse(f.child.attached(), "activation failure leaves the child detached");
	harness.expectEqual(f.state.callCount, size_t(0), "failed activation never queries the fake");
	harness.expectEqual(f.state.references, 1ul, "failed activation acquired no references");
}

TEST_CASE(ChildApoTests, testChildMissingRealtimeInterface)
{
	Fixture f;
	f.state.hasRealtime = false;
	harness.expect(f.attach() == AttachResult::NoRealtimeInterface, "missing RT interface is reported");
	harness.expectFalse(f.child.attached(), "missing RT interface detaches the child");
	harness.expectEqual(f.state.callCount, size_t(1), "no configuration query or Initialize follows RT failure");
	harness.expect(f.state.calls[0] == Call::QueryRealtime, "RT is queried first");
	harness.expectEqual(f.state.references, 1ul, "RT failure releases the activation reference");
}

TEST_CASE(ChildApoTests, testChildMissingConfigurationInterface)
{
	Fixture f;
	f.state.hasConfiguration = false;
	harness.expect(f.attach() == AttachResult::NoConfigurationInterface, "missing configuration interface is reported");
	harness.expectFalse(f.child.attached(), "configuration failure detaches even an acquired RT interface");
	harness.expectEqual(f.state.callCount, size_t(2), "Initialize is not called without configuration");
	harness.expect(f.state.calls[1] == Call::QueryConfiguration, "configuration is queried after RT");
	harness.expectEqual(f.state.references, 1ul, "configuration failure releases both acquired references");
}

TEST_CASE(ChildApoTests, testChildInitializeFailure)
{
	Fixture f;
	f.state.initializeResult = E_FAIL;
	harness.expect(f.attach() == AttachResult::InitializeFailed, "Initialize failure is reported");
	harness.expectFalse(f.child.attached(), "Initialize failure detaches the child");
	harness.expectEqual(f.state.callCount, size_t(3), "Initialize follows both interface queries");
	harness.expect(f.state.calls[2] == Call::Initialize, "Initialize is the final attach call");
	harness.expectEqual(f.state.references, 1ul, "Initialize failure releases all three references");
}

TEST_CASE(ChildApoTests, testChildSuccessfulAttachAndProcess)
{
	Fixture f;
	BYTE data[] = {1, 2, 3};
	harness.require(f.child.attach(childClsid, sizeof(data), data, f.activator()) == AttachResult::Attached, "valid child attaches");
	harness.expect(f.child.attached(), "successful attach exposes RT availability");
	harness.expectEqual(f.state.references, 4ul, "the child owns exactly three COM references");
	harness.expectEqual(f.state.dataSize, UINT32(sizeof(data)), "Initialize receives the original data size");
	harness.expect(f.state.data == data, "Initialize receives the original data pointer");
	APO_CONNECTION_PROPERTY input = {};
	APO_CONNECTION_PROPERTY output = {};
	APO_CONNECTION_PROPERTY* inputs[] = {&input, &input};
	APO_CONNECTION_PROPERTY* outputs[] = {&output};
	f.child.process(2, inputs, 1, outputs);
	harness.expectEqual(f.state.callCount, size_t(4), "process forwards exactly one call");
	harness.expect(f.state.calls[3] == Call::Process, "process calls APOProcess");
	harness.expect(f.state.inputs == inputs && f.state.outputs == outputs, "process preserves both connection arrays");
	harness.expect(f.state.inputCount == 2 && f.state.outputCount == 1, "process preserves distinct connection counts");
	f.child.reset();
	f.child.reset();
	harness.expectFalse(f.child.attached(), "reset is idempotent and detaches");
	harness.expectEqual(f.state.references, 1ul, "reset releases all three owned references");
}

TEST_CASE(ChildApoTests, testChildDestructorReleasesReferences)
{
	Fixture f;
	{
		ChildApo child;
		harness.require(child.attach(childClsid, 0, nullptr, f.activator()) == AttachResult::Attached, "scoped child attaches");
		harness.expectEqual(f.state.references, 4ul, "scoped child owns three references");
	}
	harness.expectEqual(f.state.references, 1ul, "destruction releases the three references without explicit reset");
}

TEST_CASE(ChildApoTests, testChildFormatForwarding)
{
	Fixture f;
	harness.require(f.attach() == AttachResult::Attached, "format fixture attaches");
	FakeMediaType opposite;
	FakeMediaType requested;
	IAudioMediaType* supported = nullptr;
	for (HRESULT result : {S_OK, S_FALSE, E_FAIL})
	{
		f.state.formatResult = result;
		harness.expectEqual(f.child.isInputFormatSupported(&opposite, &requested, &supported), result,
			"format forwarding preserves success, alternative-format and failure HRESULTs");
		harness.expect(f.state.oppositeFormat == &opposite && f.state.requestedFormat == &requested
			&& f.state.supportedFormat == &supported, "format forwarding preserves all three pointers");
		harness.expectEqual(f.child.attached(), SUCCEEDED(result), "only a failed format query detaches the child");
	}
	harness.expectEqual(f.state.references, 1ul, "format rejection releases all child references");
	const size_t calls = f.state.callCount;
	f.child.process(0, nullptr, 0, nullptr);
	harness.expectEqual(f.state.callCount, calls, "format rejection prevents subsequent processing");
}

TEST_CASE(ChildApoTests, testChildLatencyForwarding)
{
	Fixture f;
	harness.require(f.attach() == AttachResult::Attached, "latency fixture attaches");
	for (HRESULT result : {S_OK, E_FAIL})
	{
		HNSTIME latency = 0;
		f.state.latencyResult = result;
		harness.expectEqual(f.child.getLatency(&latency), result, "latency forwarding preserves the child's HRESULT");
		harness.expect(f.state.latency == &latency, "latency forwarding preserves the output pointer");
		harness.expectEqual(latency, HNSTIME(12345), "latency forwarding preserves the child's write");
		harness.expect(f.child.attached(), "latency failure does not detach, as in the COM shell");
	}
}

TEST_CASE(ChildApoTests, testChildSuccessfulLockAndUnlock)
{
	Fixture f;
	harness.require(f.attach() == AttachResult::Attached, "lock fixture attaches");
	APO_CONNECTION_DESCRIPTOR input = {};
	APO_CONNECTION_DESCRIPTOR output = {};
	APO_CONNECTION_DESCRIPTOR* inputs[] = {&input};
	APO_CONNECTION_DESCRIPTOR* outputs[] = {&output, &output};
	harness.expect(f.child.lockForProcess(1, inputs, 2, outputs), "successful lock is reported");
	harness.expect(f.state.inputDescriptors == inputs && f.state.outputDescriptors == outputs, "lock preserves descriptor arrays");
	harness.expect(f.state.inputCount == 1 && f.state.outputCount == 2, "lock preserves distinct connection counts");
	f.child.unlockForProcess();
	harness.expect(f.state.calls[3] == Call::Lock && f.state.calls[4] == Call::Unlock, "lock and unlock reach the configuration interface");
	harness.expect(f.child.attached(), "successful unlock retains the child for relocking");
}

TEST_CASE(ChildApoTests, testChildLockFailureDetaches)
{
	Fixture f;
	f.state.lockResult = E_FAIL;
	harness.require(f.attach() == AttachResult::Attached, "failing-lock fixture attaches");
	harness.expectFalse(f.child.lockForProcess(0, nullptr, 0, nullptr), "failed lock is reported");
	harness.expectFalse(f.child.attached(), "failed lock detaches");
	harness.expectEqual(f.state.references, 1ul, "failed lock releases all child references immediately");
	const size_t calls = f.state.callCount;
	f.child.process(0, nullptr, 0, nullptr);
	f.child.unlockForProcess();
	harness.expectEqual(f.state.callCount, calls, "a child that failed locking is neither processed nor unlocked");
}

TEST_CASE(ChildApoTests, testChildUnlockFailureDetaches)
{
	Fixture f;
	f.state.unlockResult = E_FAIL;
	harness.require(f.attach() == AttachResult::Attached, "failing-unlock fixture attaches");
	harness.require(f.child.lockForProcess(0, nullptr, 0, nullptr), "child locks before the failing unlock");
	f.child.unlockForProcess();
	harness.expectFalse(f.child.attached(), "failed unlock detaches");
	harness.expectEqual(f.state.references, 1ul, "failed unlock releases all child references immediately");
	const size_t calls = f.state.callCount;
	f.child.process(0, nullptr, 0, nullptr);
	f.child.unlockForProcess();
	harness.expectEqual(f.state.callCount, calls, "detached child receives no later processing or unlock calls");
}

TEST_CASE(ChildApoTests, testChildDetachedCallsAreNoOps)
{
	Fixture f;
	f.child.process(0, nullptr, 0, nullptr);
	f.child.unlockForProcess();
	harness.expectFalse(f.child.lockForProcess(0, nullptr, 0, nullptr), "a detached child cannot lock");
	HNSTIME latency = 42;
	harness.expectEqual(f.child.getLatency(&latency), E_UNEXPECTED, "detached latency has no child result");
	harness.expectEqual(latency, HNSTIME(42), "detached latency leaves the shell's default untouched");
	harness.expectEqual(f.child.isInputFormatSupported(nullptr, nullptr, nullptr), E_UNEXPECTED, "detached format query has no child result");
	harness.expectEqual(f.state.callCount, size_t(0), "detached calls do not touch the fake");
}

TEST_CASE(ChildApoTests, testChildReattachReleasesPreviousInterfaces)
{
	Fixture f;
	harness.require(f.attach() == AttachResult::Attached, "first attach succeeds");
	harness.require(f.attach() == AttachResult::Attached, "reattach succeeds");
	harness.expectEqual(f.state.references, 4ul, "reattach replaces rather than accumulates references");
	harness.expect(f.child.attach(L"", 0, nullptr, f.activator()) == AttachResult::NoChild, "empty reattach detaches");
	harness.expectEqual(f.state.references, 1ul, "empty reattach releases the old child");
	harness.require(f.attach() == AttachResult::Attached, "attach after reset succeeds");
	f.state.initializeResult = E_FAIL;
	harness.expect(f.attach() == AttachResult::InitializeFailed, "failed replacement reports Initialize failure");
	harness.expectFalse(f.child.attached(), "failed replacement does not retain the old child");
	harness.expectEqual(f.state.references, 1ul, "failed replacement releases old and new references");
}
}
