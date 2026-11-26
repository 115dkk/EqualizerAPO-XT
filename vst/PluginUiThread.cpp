/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "PluginUiThread.h"
#include "VSTPluginInstance.h"

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

namespace PluginUiThread
{
namespace
{
struct Registration
{
	explicit Registration(Runner* value) : runner(value) {}
	Runner* runner;
	size_t active = 0;
};
std::mutex registryMutex;
std::condition_variable idle;
std::shared_ptr<Registration> current;

class Use
{
public:
	Use()
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		entry = current;
		if (entry)
			++entry->active;
	}
	~Use()
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		if (entry && --entry->active == 0)
			idle.notify_all();
	}
	Use(const Use&) = delete;
	Use& operator=(const Use&) = delete;
	Runner* get() const { return entry ? entry->runner : nullptr; }
private:
	std::shared_ptr<Registration> entry;
};
}

void setRunner(Runner* runner)
{
	auto replacement = runner ? std::make_shared<Registration>(runner) : nullptr;
	std::unique_lock<std::mutex> lock(registryMutex);
	if (current && current->runner == runner)
		return;
	auto previous = std::exchange(current, std::move(replacement));
	// Never hold the registry lock across work or a wait in Runner::call.
	// Nested UI-thread initialization can therefore enter run() as well.
	idle.wait(lock, [&] { return !previous || previous->active == 0; });
}

bool run(const std::function<void()>& work)
{
	Use use;
	Runner* runner = use.get();
	if (runner == nullptr || runner->isUiThread())
	{
		work();
		return true;
	}
	std::exception_ptr error;
	const bool accepted = runner->call([&] {
		try { work(); }
		catch (...) { error = std::current_exception(); }
	});
	if (error)
		std::rethrow_exception(error);
	return accepted;
}

void retire(VSTPluginInstance* instance) noexcept
{
	if (instance == nullptr)
		return;
	try
	{
		Use use;
		Runner* runner = use.get();
		if (runner != nullptr && !runner->isUiThread()
			&& runner->post([instance] { delete instance; }))
			return;
	}
	catch (...)
	{
		// Queue allocation can fail during shutdown; do not leak the instance.
	}
	delete instance;
}
}
