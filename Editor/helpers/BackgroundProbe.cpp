/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BackgroundProbe.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

namespace
{
class Queue;

// Completions are posted to this child of the running QCoreApplication. It is
// destroyed while the application object still exists, and main's restart
// loop builds a new application, so the next request makes a new receiver.
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
	void run(QObject* context, std::function<void()> work, std::function<void()> done)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stopping)
				return;
			if (receiver == nullptr)
				receiver = new Receiver(*this);
			if (!worker.joinable())
				worker = std::thread([this]() { loop(); });
			Item item;
			item.context = context;
			item.work = std::move(work);
			item.done = std::move(done);
			queued.push_back(std::move(item));
		}
		condition.notify_one();
	}

	// Completions already posted to the receiver go with it.
	void forget(const Receiver* gone)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (receiver == gone)
			receiver = nullptr;
	}

	void shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
			queued.clear();
		}
		condition.notify_one();
		if (worker.joinable())
			worker.join();
	}

private:
	struct Item
	{
		// Read on the UI thread only, when the completion runs.
		QPointer<QObject> context;
		std::function<void()> work;
		std::function<void()> done;
	};

	void loop()
	{
		// The endpoint probes need COM on this thread; the multithreaded
		// apartment needs no message pump.
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		for (;;)
		{
			Item item;
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, [this]() { return stopping || !queued.empty(); });
				if (stopping)
					break;
				item = std::move(queued.front());
				queued.pop_front();
			}

			try
			{
				item.work();
			}
			catch (...)
			{
				// A probe that throws reports its default result.
			}

			// Posting under the lock keeps the receiver alive until the event
			// is queued: its destructor takes the same lock.
			std::lock_guard<std::mutex> lock(mutex);
			if (receiver == nullptr)
				continue;
			QMetaObject::invokeMethod(receiver,
				[context = std::move(item.context), done = std::move(item.done)]() {
					if (!context.isNull())
						done();
				}, Qt::QueuedConnection);
		}
		if (SUCCEEDED(com))
			CoUninitialize();
	}

	std::mutex mutex;
	std::condition_variable condition;
	std::deque<Item> queued;
	Receiver* receiver = nullptr;
	bool stopping = false;
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

void BackgroundProbe::run(QObject* context, std::function<void()> work, std::function<void()> done)
{
	queue().run(context, std::move(work), std::move(done));
}

void BackgroundProbe::shutdown()
{
	if (existing != nullptr)
		existing->shutdown();
}
