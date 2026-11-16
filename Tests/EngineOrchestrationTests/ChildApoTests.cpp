/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <array>
#include <cmath>
#include "EqualizerAPO/ChildApo.h"
#include <ks.h>
#include <ksmedia.h>
#include "platform/windows/WindowsPath.h"
#include "../TestChildApo/TestChildApoProtocol.h"
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

// Like the VST fixtures, this DLL is staged next to the executable. No COM
// registration, endpoint property store, or audio service is used (audit #467 TD-26).
class LoadedChildFixture
{
public:
	using GetClassObject = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
	using CanUnloadNow = HRESULT (WINAPI*)();

	LoadedChildFixture()
	{
		auto& harness = test::activeHarness();
		const std::wstring directory = pathutil::exeDirectory();
		harness.require(!directory.empty(), "fixture executable directory is available");
		module = LoadLibraryW((directory + L"\\TestChildApo.dll").c_str());
		harness.require(module != nullptr, "TestChildApo.dll is present next to the test executable and loads without registration");
		getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(module, "DllGetClassObject"));
		canUnloadNow = reinterpret_cast<CanUnloadNow>(GetProcAddress(module, "DllCanUnloadNow"));
	}

	~LoadedChildFixture()
	{
		if (locked)
			child.unlockForProcess();
		child.reset();
		if (canUnloadNow != nullptr)
			test::activeHarness().expectEqual(canUnloadNow(), S_OK, "fixture has no live objects or module locks before FreeLibrary");
		if (module != nullptr)
			FreeLibrary(module);
	}

	LoadedChildFixture(const LoadedChildFixture&) = delete;
	LoadedChildFixture& operator=(const LoadedChildFixture&) = delete;

	ChildApo::Activator activator() const
	{
		return [this](REFCLSID clsid, IAudioProcessingObject** object) -> HRESULT {
			if (getClassObject == nullptr)
				return E_NOINTERFACE;
			winutil::ComPtr<IClassFactory> factory;
			const HRESULT hr = getClassObject(clsid, __uuidof(IClassFactory), reinterpret_cast<void**>(factory.put()));
			if (FAILED(hr))
				return hr;
			return factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(object));
		};
	}

	ChildApo child;
	GetClassObject getClassObject = nullptr;
	CanUnloadNow canUnloadNow = nullptr;
	bool locked = false;

private:
	HMODULE module = nullptr;
};

HRESULT makeChildMediaType(UINT32 bytes, IAudioMediaType** type, UINT32 channels = 2, FLOAT rate = 48000.0f)
{
	UNCOMPRESSEDAUDIOFORMAT format = {};
	format.guidFormatType = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	format.dwSamplesPerFrame = channels;
	format.dwBytesPerSampleContainer = bytes;
	format.dwValidBitsPerSample = bytes * 8;
	format.fFramesPerSecond = rate;
	format.dwChannelMask = channels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : 0;
	return CreateAudioMediaTypeFromUncompressedAudioFormat(&format, type);
}

template<typename Sample>
void realChildRound()
{
	auto& harness = test::activeHarness();
	LoadedChildFixture f;
	APOInitSystemEffects init = {};
	init.APOInit.cbSize = sizeof(init);
	init.APOInit.clsid = kTestChildApoClsid;
	harness.require(f.child.attach(kTestChildApoClsidText, sizeof(init), reinterpret_cast<BYTE*>(&init), f.activator())
		== AttachResult::Attached, "real child attaches with the product-shaped legacy initialization blob");
	harness.require(f.canUnloadNow != nullptr, "fixture exports DllCanUnloadNow");
	harness.expectEqual(f.canUnloadNow(), S_FALSE, "attached real child keeps the module alive");

	winutil::ComPtr<IAudioMediaType> format;
	winutil::ComPtr<IAudioMediaType> suggested;
	harness.requireEqual(makeChildMediaType(sizeof(Sample), format.put()), S_OK, "create 48 kHz stereo float media type");
	harness.requireEqual(f.child.isInputFormatSupported(format.get(), format.get(), suggested.put()), S_OK,
		"real child accepts matching float input and output");
	harness.require(suggested != nullptr, "successful format negotiation returns a referenced media type");

	alignas(16) std::array<Sample, 8> input = {Sample(-1), Sample(0.75), Sample(-0.5), Sample(0.25),
		Sample(0), Sample(0.125), Sample(0.5), Sample(1)};
	alignas(16) std::array<Sample, 8> output = {};
	const auto original = input;
	APO_CONNECTION_DESCRIPTOR inDesc = {};
	inDesc.Type = APO_CONNECTION_BUFFER_TYPE_EXTERNAL;
	inDesc.pBuffer = reinterpret_cast<UINT_PTR>(input.data());
	inDesc.pFormat = format.get();
	inDesc.u32MaxFrameCount = 4;
	inDesc.u32Signature = APO_CONNECTION_DESCRIPTOR_SIGNATURE;
	APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
	outDesc.pBuffer = reinterpret_cast<UINT_PTR>(output.data());
	APO_CONNECTION_DESCRIPTOR* inputs[] = {&inDesc};
	APO_CONNECTION_DESCRIPTOR* outputs[] = {&outDesc};
	APO_CONNECTION_PROPERTY in = {};
	in.pBuffer = inDesc.pBuffer;
	in.u32ValidFrameCount = 3;
	in.u32BufferFlags = BUFFER_VALID;
	in.u32Signature = APO_CONNECTION_PROPERTY_SIGNATURE;
	APO_CONNECTION_PROPERTY out = {};
	out.pBuffer = outDesc.pBuffer;
	out.u32Signature = APO_CONNECTION_PROPERTY_SIGNATURE;
	APO_CONNECTION_PROPERTY* inProps[] = {&in};
	APO_CONNECTION_PROPERTY* outProps[] = {&out};
	const double gain = std::pow(10.0, kTestChildApoGainDb / 20.0);

	// Relock with aliased buffers as well: APO_FLAG_INPLACE is an actual promise.
	for (bool inPlace : {false, true})
	{
		input = original;
		output.fill(Sample(9));
		outDesc.pBuffer = inPlace ? inDesc.pBuffer : reinterpret_cast<UINT_PTR>(output.data());
		out.pBuffer = outDesc.pBuffer;
		in.u32BufferFlags = BUFFER_VALID;
		out.u32BufferFlags = BUFFER_INVALID;
		f.locked = f.child.lockForProcess(1, inputs, 1, outputs);
		harness.require(f.locked, "real child locks matching float connection descriptors");
		f.child.process(1, inProps, 1, outProps);
		const auto& processed = inPlace ? input : output;
		for (size_t i = 0; i < 6; ++i)
			harness.expectNear(processed[i], original[i] * gain, 1e-6, "real child applies the declared gain to each valid sample");
		harness.expectEqual(processed[6], inPlace ? original[6] : Sample(9), "processing stops at the valid frame count");
		harness.expectEqual(out.u32ValidFrameCount, in.u32ValidFrameCount, "real child preserves valid frames");
		harness.expectEqual(out.u32BufferFlags, BUFFER_VALID, "real child marks processed output valid");
		const auto beforeSilent = processed;
		in.u32BufferFlags = BUFFER_SILENT;
		f.child.process(1, inProps, 1, outProps);
		harness.expectEqual(out.u32BufferFlags, BUFFER_SILENT, "real child preserves silence");
		harness.expectEqual(out.u32ValidFrameCount, in.u32ValidFrameCount, "silent buffers preserve valid frames");
		// The dynamically loaded APO can mutate processed through out.pBuffer (UINT_PTR).
		// cppcheck-suppress knownConditionTrueFalse
		harness.expect(processed == beforeSilent, "silent processing does not modify sample storage");
		f.child.unlockForProcess();
		// LoadedChildFixture's destructor reads this to avoid a second unlock.
		// cppcheck-suppress unreadVariable
		f.locked = false;
		harness.expect(f.child.attached(), "real child remains attached after unlocking");
	}
	f.child.reset();
	harness.expectEqual(f.canUnloadNow(), S_OK, "unlock and reset release every real child interface");
}

TEST_CASE(ChildApoTests, testRealChildFloat32)
{
	realChildRound<float>();
}

TEST_CASE(ChildApoTests, testRealChildFloat64)
{
	realChildRound<double>();
}

TEST_CASE(ChildApoTests, testRealChildInitializationShapes)
{
	LoadedChildFixture f;
	harness.require(f.child.attach(kTestChildApoClsidText, 0, nullptr, f.activator()) == AttachResult::Attached,
		"real child accepts an empty initialization blob");
	APOInitSystemEffects2 init = {};
	init.APOInit.cbSize = sizeof(init);
	// The product forwards its own CLSID unchanged, not the child's CLSID.
	init.APOInit.clsid = childGuid;
	init.AudioProcessingMode = AUDIO_SIGNALPROCESSINGMODE_DEFAULT;
	harness.require(f.child.attach(kTestChildApoClsidText, sizeof(init), reinterpret_cast<BYTE*>(&init), f.activator())
		== AttachResult::Attached, "real child accepts modern initialization with null stores and the parent's CLSID");
}

TEST_CASE(ChildApoTests, testRealChildFactoryAndRegistration)
{
	LoadedChildFixture f;
	harness.require(f.getClassObject != nullptr && f.canUnloadNow != nullptr, "fixture exports both COM entry points");
	harness.expectEqual(f.canUnloadNow(), S_OK, "loading alone creates no fixture objects");
	winutil::ComPtr<IClassFactory> factory;
	harness.expectEqual(f.getClassObject(childGuid, __uuidof(IClassFactory), reinterpret_cast<void**>(factory.put())),
		CLASS_E_CLASSNOTAVAILABLE, "factory rejects unrelated CLSIDs");
	harness.requireEqual(f.getClassObject(kTestChildApoClsid, __uuidof(IClassFactory), reinterpret_cast<void**>(factory.put())),
		S_OK, "fixture supplies a class factory");
	harness.expectEqual(f.canUnloadNow(), S_FALSE, "class factory reference prevents unloading");
	harness.expectEqual(factory->LockServer(TRUE), S_OK, "server lock succeeds");
	factory.reset();
	harness.expectEqual(f.canUnloadNow(), S_FALSE, "server lock outlives the factory reference");
	// Reacquire a factory to balance the module-wide lock after releasing its creator.
	const HRESULT factoryResult = f.getClassObject(kTestChildApoClsid, __uuidof(IClassFactory), reinterpret_cast<void**>(factory.put()));
	harness.requireEqual(factoryResult, S_OK, "retrieve factory to release the server lock");
	harness.expectEqual(factory->LockServer(FALSE), S_OK, "server unlock succeeds");
	winutil::ComPtr<IAudioProcessingObject> apo;
	harness.expectEqual(factory->CreateInstance(factory.get(), __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(apo.put())),
		CLASS_E_NOAGGREGATION, "fixture does not support COM aggregation");
	harness.requireEqual(factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(apo.put())),
		S_OK, "factory creates the real APO");
	factory.reset();
	winutil::ComPtr<IAudioSystemEffects> effects;
	harness.requireEqual(apo->QueryInterface(__uuidof(IAudioSystemEffects), reinterpret_cast<void**>(effects.put())),
		S_OK, "fixture exposes the legacy system-effect marker");
	winutil::ComPtr<IUnknown> identity;
	winutil::ComPtr<IUnknown> effectIdentity;
	harness.requireEqual(apo->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(identity.put())), S_OK, "APO has COM identity");
	harness.requireEqual(effects->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(effectIdentity.put())), S_OK, "marker has COM identity");
	harness.expect(identity.get() == effectIdentity.get(), "all APO interfaces share one COM identity");
	APO_REG_PROPERTIES* registration = nullptr;
	harness.requireEqual(apo->GetRegistrationProperties(&registration), S_OK, "fixture returns base registration properties");
	harness.require(registration != nullptr, "registration properties were allocated");
	harness.expect(registration->clsid == kTestChildApoClsid, "registration identifies the fixture");
	harness.expectEqual(registration->Flags, static_cast<APO_FLAG>(APO_FLAG_DEFAULT | APO_FLAG_INPLACE), "registration promises matching formats and in-place processing");
	harness.expect(registration->u32MinInputConnections == 1 && registration->u32MaxInputConnections == 1
		&& registration->u32MinOutputConnections == 1 && registration->u32MaxOutputConnections == 1,
		"system effect has exactly one input and one output");
	harness.expect(registration->u32NumAPOInterfaces == 1 && registration->iidAPOInterfaceList[0] == __uuidof(IAudioProcessingObject),
		"registration lists the processing interface as in the SDK sample");
	CoTaskMemFree(registration);
}

TEST_CASE(ChildApoTests, testRealChildFormatContract)
{
	LoadedChildFixture f;
	winutil::ComPtr<IAudioProcessingObject> apo;
	harness.requireEqual(f.activator()(kTestChildApoClsid, apo.put()), S_OK, "create real child for output-format negotiation");
	harness.requireEqual(apo->Initialize(0, nullptr), S_OK, "initialize format fixture");
	winutil::ComPtr<IAudioMediaType> stereo;
	winutil::ComPtr<IAudioMediaType> mono;
	winutil::ComPtr<IAudioMediaType> otherRate;
	winutil::ComPtr<IAudioMediaType> wide;
	winutil::ComPtr<IAudioMediaType> suggested;
	harness.requireEqual(makeChildMediaType(4, stereo.put()), S_OK, "create stereo float32");
	harness.requireEqual(makeChildMediaType(4, mono.put(), 1), S_OK, "create mono float32");
	harness.requireEqual(makeChildMediaType(4, otherRate.put(), 2, 96000.0f), S_OK, "create 96 kHz float32");
	harness.requireEqual(makeChildMediaType(8, wide.put()), S_OK, "create stereo float64");
	for (IAudioMediaType* type : {stereo.get(), mono.get(), otherRate.get(), wide.get()})
	{
		harness.expectEqual(apo->IsInputFormatSupported(nullptr, type, suggested.put()), S_OK, "input negotiation accepts a supported format before output is known");
		harness.expectEqual(apo->IsOutputFormatSupported(type, type, suggested.put()), S_OK, "output negotiation accepts matching formats");
	}
	for (IAudioMediaType* type : {mono.get(), otherRate.get(), wide.get()})
	{
		harness.expectEqual(apo->IsInputFormatSupported(stereo.get(), type, suggested.put()), APOERR_FORMAT_NOT_SUPPORTED,
			"input negotiation rejects mismatched channels, rate or depth");
		harness.expect(suggested == nullptr, "rejected input leaves no stale suggestion");
		harness.expectEqual(apo->IsOutputFormatSupported(stereo.get(), type, suggested.put()), APOERR_FORMAT_NOT_SUPPORTED,
			"output negotiation rejects mismatched channels, rate or depth");
	}
	UNCOMPRESSEDAUDIOFORMAT pcm = {};
	harness.requireEqual(stereo->GetUncompressedAudioFormat(&pcm), S_OK, "retrieve format for integer negative control");
	pcm.guidFormatType = KSDATAFORMAT_SUBTYPE_PCM;
	winutil::ComPtr<IAudioMediaType> integer;
	harness.requireEqual(CreateAudioMediaTypeFromUncompressedAudioFormat(&pcm, integer.put()), S_OK, "create integer PCM negative control");
	harness.expectEqual(apo->IsInputFormatSupported(integer.get(), integer.get(), suggested.put()), APOERR_FORMAT_NOT_SUPPORTED,
		"integer PCM is not advertised as float");
	harness.expectEqual(apo->IsOutputFormatSupported(integer.get(), integer.get(), suggested.put()), APOERR_FORMAT_NOT_SUPPORTED,
		"integer PCM is rejected for output too");
}

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
