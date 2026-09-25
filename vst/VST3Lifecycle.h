/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <atomic>
#include <functional>
#include <mutex>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

class VST3Lifecycle
{
public:
	using HasPendingChanges = std::function<bool()>;
	using PrepareParameterChanges = std::function<Steinberg::Vst::IParameterChanges*()>;

	void setInterfaces(Steinberg::Vst::IComponent* component,
		Steinberg::Vst::IAudioProcessor* processor);
	void clearInterfaces();

	bool setupProcessing(const Steinberg::Vst::ProcessSetup& setup);
	bool prepareOnce(Steinberg::int32 symbolicSampleSize);
	bool activate();
	void deactivate();
	bool startProcessing();
	void stopProcessing();
	bool beginEditorSession(Steinberg::int32 symbolicSampleSize);
	void endEditorSession();
	bool flushParameters(Steinberg::Vst::ProcessData& data,
		const HasPendingChanges& hasPendingChanges,
		const PrepareParameterChanges& prepareParameterChanges,
		Steinberg::int32 symbolicSampleSize);

	bool audioProcessing() const noexcept;
	bool canProcessNow() const;

private:
	bool prepareOnceLocked(Steinberg::int32 symbolicSampleSize);
	bool activateLocked();
	void deactivateLocked();

	Steinberg::Vst::IComponent* component = nullptr;
	Steinberg::Vst::IAudioProcessor* processor = nullptr;
	bool prepared = false;
	bool active = false;
	std::atomic<bool> processing{false};
	bool editorSession = false;
	std::atomic<bool> parameterFlushInProgress{false};
	mutable std::mutex mutex;
};
