/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PluginLoadQueue.h"
#include "CrashHandler.h"
#include "platform/windows/ThreadWait.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>
#include <QtGui/private/qguiapplication_p.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <UIAutomation.h>

#include "services/logging/Logging.h"

namespace
{
using Callback = std::function<void(AbstractLibrary::InitializeResult result)>;

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

// Ends a waitForPendingLoads that is asleep in processEvents. Delivering a
// posted completion does not make processEvents(WaitForMoreEvents) return:
// the dispatcher sends the posted events and then goes on waiting for a
// native one, which here was the deadline timer 30 s later (the VST
// self-test waited 30 s per case for loads that had failed in milliseconds,
// and its CI step timed out; see testVstFill's load-wait check). interrupt()
// is the dispatcher's documented, thread-safe way to make processEvents
// return. Nobody waiting: nothing to do, the main loop is left alone.
std::atomic<int> pendingLoadWaiters{0};

void wakePendingLoadWaiters()
{
	if (pendingLoadWaiters.load() == 0)
		return;
	if (QAbstractEventDispatcher* dispatcher = QCoreApplication::eventDispatcher())
		dispatcher->interrupt();
}

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

			if (stopping)
			{
				// The loader thread has ended, so this load would never run and
				// the row would keep saying "Loading plugin..." for good. It
				// completes as a failed load instead, through the receiver like
				// every other completion.
				pendingDeliveries++;
				QMetaObject::invokeMethod(receiver, [this, callback = std::move(callback)]() {
					callback(AbstractLibrary::InitializeResult::LoadingFailed);
					{
						std::lock_guard<std::mutex> deliveryLock(mutex);
						pendingDeliveries--;
					}
					wakePendingLoadWaiters();
				}, Qt::QueuedConnection);
				return;
			}

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

	bool shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
			for (const std::shared_ptr<Request>& request : queued)
				requests.erase(request->library.get());
			queued.clear();
		}
		condition.notify_one();
		return winutil::joinThreadFor(worker, PluginLoadQueue::kShutdownTimeoutMs);
	}

	std::wstring shutdownWorkName() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return activePath.empty() ? L"plug-in loader" : L"loading plug-in " + activePath;
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
				activePath = request->library->getLoadPath();
			}

			const std::wstring path = request->library->getLoadPath();
			TraceFStatic(L"Loading plug-in library %s on the plug-in loader thread", path.c_str());
			const auto started = std::chrono::steady_clock::now();
			AbstractLibrary::InitializeResult result = AbstractLibrary::InitializeResult::LoadingFailed;
			try
			{
				CrashHandler::setBreadcrumb(L"loading plug-in " + path);
				result = VSTPluginLibrary::load(request->library->getLibPath()).status;
			}
			catch (...)
			{
				result = AbstractLibrary::InitializeResult::LoadingFailed;
			}
			const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			LogFStatic(L"Plug-in library %s loaded in %lld ms, result %d", path.c_str(), elapsedMs, static_cast<int>(result));

			// Posting under the lock keeps the receiver alive until the events
			// are queued: its destructor takes the same lock.
			std::lock_guard<std::mutex> lock(mutex);
			std::vector<Callback> callbacks = std::move(request->callbacks);
			requests.erase(request->library.get());
			if (receiver == nullptr)
			{
				pendingDeliveries--;
				wakePendingLoadWaiters();
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
							{
								std::lock_guard<std::mutex> deliveryLock(mutex);
								pendingDeliveries--;
							}
							wakePendingLoadWaiters();
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
	// Retain the path through native thread teardown as well as load().
	std::wstring activePath;
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
	std::function<void(AbstractLibrary::InitializeResult result)> onDoneOnUiThread)
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

bool PluginLoadQueue::waitForPendingLoads(int timeoutMs)
{
	if (pendingCount() == 0)
		return true;

	// Blocks in the event dispatcher until something arrives (a completion
	// posted by the loader thread wakes it, and the deadline timer bounds the
	// wait) instead of polling with short sleeps. processEvents, not a nested
	// QEventLoop, so deferred deletions behave as they did for the callers.
	// The loop ends on the timer's own timeout: comparing a clock instead
	// could see the single-shot fire just before the clock reaches the limit
	// and then block with nothing left to wake it.
	bool expired = false;
	QTimer deadline;
	deadline.setSingleShot(true);
	deadline.setTimerType(Qt::PreciseTimer);
	QObject::connect(&deadline, &QTimer::timeout, [&expired]() { expired = true; });
	deadline.start(timeoutMs);
	// A completion that brings the count to zero interrupts the dispatcher
	// while this is set (wakePendingLoadWaiters); the loop then sees the
	// count and leaves instead of sleeping on to the deadline.
	pendingLoadWaiters.fetch_add(1);
	while (pendingCount() != 0 && !expired)
		QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
	pendingLoadWaiters.fetch_sub(1);
	return pendingCount() == 0;
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

bool PluginLoadQueue::shutdown()
{
	return existing == nullptr || existing->shutdown();
}

std::wstring PluginLoadQueue::shutdownWorkName()
{
	return existing == nullptr ? L"plug-in loader" : existing->shutdownWorkName();
}
