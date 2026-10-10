/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PluginLoadQueue.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QSemaphore>
#include <QThreadPool>
#include <QtGui/private/qguiapplication_p.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <UIAutomation.h>

#include "services/logging/Logging.h"

namespace
{
using Callback = std::function<void(int result)>;

class Queue;

// Completions are posted to this child of the running QCoreApplication. It is
// destroyed while the application object still exists, and main's restart
// loop builds a new application, so the next enqueue makes a new receiver.
class Receiver : public QObject
{
public:
	explicit Receiver(Queue& owningQueue)
		: QObject(QCoreApplication::instance()), owner(owningQueue)
	{
	}

	~Receiver() override;

private:
	Queue& owner;
};

class Queue
{
public:
	Queue()
	{
		// One worker: a second would only wait for the same process loader lock.
		worker = std::thread([this]() { run(); });
	}

	void enqueue(const std::shared_ptr<VSTPluginLibrary>& library, Callback callback)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (receiver == nullptr)
				receiver = new Receiver(*this);

			auto found = requests.find(library.get());
			if (found != requests.end())
			{
				found->second->callbacks.push_back(std::move(callback));
				return;
			}

			auto request = std::make_shared<Request>();
			request->library = library;
			request->callbacks.push_back(std::move(callback));
			requests.emplace(library.get(), request);
			queued.push_back(std::move(request));
			pendingDeliveries++;
		}
		condition.notify_one();
	}

	bool isPending(const VSTPluginLibrary* library) const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return requests.contains(library);
	}

	int pendingCount() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return pendingDeliveries;
	}

	// Callbacks already posted to the receiver go with it; only the loads
	// still queued or running are left to deliver.
	void forget(const Receiver* gone)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (receiver != gone)
			return;
		receiver = nullptr;
		pendingDeliveries = static_cast<int>(requests.size());
	}

	void setHeld(bool hold)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			held = hold;
		}
		condition.notify_one();
	}

	void shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
			for (const std::shared_ptr<Request>& request : queued)
				requests.erase(request->library.get());
			queued.clear();
		}
		condition.notify_one();
		if (worker.joinable())
			worker.join();
	}

private:
	struct Request
	{
		std::shared_ptr<VSTPluginLibrary> library;
		std::vector<Callback> callbacks;
	};

	void run()
	{
		for (;;)
		{
			std::shared_ptr<Request> request;
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, [this]() { return stopping || (!held && !queued.empty()); });
				if (stopping)
					return;
				request = queued.front();
				queued.pop_front();
			}

			const std::wstring path = request->library->getLoadPath();
			TraceFStatic(L"Loading plug-in library %s on the plug-in loader thread", path.c_str());
			const auto started = std::chrono::steady_clock::now();
			int result = AbstractLibrary::LOADING_FAILED;
			try
			{
				result = request->library->initialize();
			}
			catch (...)
			{
				result = AbstractLibrary::LOADING_FAILED;
			}
			const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			LogFStatic(L"Plug-in library %s loaded in %lld ms, result %d", path.c_str(), elapsedMs, result);

			// Posting under the lock keeps the receiver alive until the events
			// are queued: its destructor takes the same lock.
			std::lock_guard<std::mutex> lock(mutex);
			std::vector<Callback> callbacks = std::move(request->callbacks);
			requests.erase(request->library.get());
			if (receiver == nullptr)
			{
				pendingDeliveries--;
				continue;
			}
			// Every delivery runs on the UI thread, so the count needs no atomic.
			auto remaining = std::make_shared<size_t>(callbacks.size());
			for (Callback& callback : callbacks)
			{
				QMetaObject::invokeMethod(receiver,
					[this, callback = std::move(callback), remaining, result]() {
						callback(result);
						if (--*remaining == 0)
						{
							std::lock_guard<std::mutex> deliveryLock(mutex);
							pendingDeliveries--;
						}
					}, Qt::QueuedConnection);
			}
		}
	}

	mutable std::mutex mutex;
	std::condition_variable condition;
	std::deque<std::shared_ptr<Request>> queued;
	std::unordered_map<const VSTPluginLibrary*, std::shared_ptr<Request>> requests;
	Receiver* receiver = nullptr;
	int pendingDeliveries = 0;
	bool stopping = false;
	bool held = false;
	std::thread worker;
};

Receiver::~Receiver()
{
	owner.forget(this);
}

// Touched on the UI thread only; never deleted, so a late call after
// shutdown still finds the object.
Queue* existing = nullptr;

Queue& queue()
{
	if (existing == nullptr)
		existing = new Queue();
	return *existing;
}
}

void PluginLoadQueue::enqueue(const std::shared_ptr<VSTPluginLibrary>& library,
	std::function<void(int result)> onDoneOnUiThread)
{
	queue().enqueue(library, std::move(onDoneOnUiThread));
}

bool PluginLoadQueue::isPending(const VSTPluginLibrary* library)
{
	return queue().isPending(library);
}

int PluginLoadQueue::pendingCount()
{
	return queue().pendingCount();
}

void PluginLoadQueue::holdLoads()
{
	queue().setHeld(true);
}

void PluginLoadQueue::releaseLoads()
{
	queue().setHeld(false);
}

void PluginLoadQueue::startUiAutomation(WId shownWindow)
{
	// COM loads it on the first cross-process UIA call. Held for the process,
	// like COM would hold it.
	static const HMODULE proxyStub = LoadLibraryW(L"onecorecommonproxystub.dll");
	(void)proxyStub;
	// The documented release call for a window that has returned no provider.
	// Qt's accessibility stays off until a real client asks. Only a shown
	// top-level window started UIA's thread: the same call on a message-only
	// or a hidden pop-up window left the stall in place.
	UiaReturnRawElementProvider(reinterpret_cast<HWND>(shownWindow), 0, 0, nullptr);
}

static void keepGuiThreadPoolStarted()
{
	// Null when QT_NO_GUI_THREADPOOL is set; then fills stay on the caller.
	QThreadPool* pool = QGuiApplicationPrivate::qtGuiThreadPool();
	if (pool == nullptr)
		return;
	pool->setExpiryTimeout(-1);
	// Every task blocks until all have started, so the pool has to start one
	// thread per task. An idle pool thread is reused before a new one is made.
	const int count = pool->maxThreadCount();
	QSemaphore running;
	QSemaphore release;
	QSemaphore finished;
	for (int i = 0; i < count; i++)
	{
		pool->start([&running, &release, &finished]() {
			running.release();
			release.acquire();
			finished.release();
		});
	}
	running.acquire(count);
	release.release(count);
	finished.acquire(count);
}

void PluginLoadQueue::prepareUiThread()
{
	keepGuiThreadPoolStarted();
}

void PluginLoadQueue::shutdown()
{
	if (existing != nullptr)
		existing->shutdown();
}
