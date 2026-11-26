/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PluginUiThreadBridge.h"
#include <QScopeGuard>
#include <algorithm>

namespace
{
// Accessed only on the application's main thread. Other threads reach the
// bridge through PluginUiThread's synchronized runner registration.
PluginUiThreadBridge* bridge = nullptr;
}

void PluginUiThreadBridge::install()
{
	Q_ASSERT(QThread::isMainThread());
	if (bridge == nullptr)
	{
		bridge = new PluginUiThreadBridge();
		PluginUiThread::setRunner(bridge);
	}
}

void PluginUiThreadBridge::uninstall()
{
	Q_ASSERT(QThread::isMainThread());
	if (bridge == nullptr)
		return;
	{
		std::lock_guard<std::mutex> lock(bridge->mutex);
		bridge->accepting = false;
	}
	bridge->drain();
	PluginUiThread::setRunner(nullptr);
	delete bridge;
	bridge = nullptr;
}

bool PluginUiThreadBridge::isUiThread() const
{
	return QThread::currentThread() == thread();
}

bool PluginUiThreadBridge::enqueue(Task task)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (!accepting)
		return false;
	queued.push_back(std::move(task));
	if (!QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection))
	{
		queued.pop_back();
		return false;
	}
	return true;
}

bool PluginUiThreadBridge::call(const std::function<void()>& work)
{
	auto completion = std::make_shared<Completion>();
	if (!enqueue({work, completion}))
		return false;
	std::unique_lock<std::mutex> lock(completion->mutex);
	completion->condition.wait(lock, [&] { return completion->done; });
	if (completion->error)
		std::rethrow_exception(completion->error);
	return true;
}

bool PluginUiThreadBridge::post(std::function<void()> work)
{
	return enqueue({std::move(work), nullptr});
}

void PluginUiThreadBridge::drain()
{
	Q_ASSERT(isUiThread());
	if (draining)
		return;
	draining = true;
	const auto reset = qScopeGuard([this] { draining = false; });
	for (;;)
	{
		Task task;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (queued.empty())
				return;
			task = std::move(queued.front());
			queued.pop_front();
		}
		std::exception_ptr error;
		try { task.work(); }
		catch (...) { error = std::current_exception(); }
		if (task.completion)
		{
			std::lock_guard<std::mutex> lock(task.completion->mutex);
			task.completion->error = error;
			task.completion->done = true;
			task.completion->condition.notify_one();
		}
		else if (error)
			qCritical("Asynchronous plug-in UI work threw an exception");
	}
}

bool PluginUiThreadBridge::waitServicing(QThread& thread, QDeadlineTimer deadline)
{
	if (!QThread::isMainThread() || bridge == nullptr)
		return thread.wait(deadline);
	for (;;)
	{
		bridge->drain();
		if (thread.wait(0))
		{
			// The worker may have posted its final retirement just before exit.
			bridge->drain();
			return true;
		}
		if (deadline.hasExpired())
			return false;
		const qint64 remaining = deadline.remainingTime();
		thread.wait(static_cast<unsigned long>(remaining < 0 ? 5 : (std::min)(qint64(5), remaining)));
	}
}
