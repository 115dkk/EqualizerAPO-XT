/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "vst/PluginUiThread.h"
#include <QDeadlineTimer>
#include <QObject>
#include <QThread>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>

class PluginUiThreadBridge final : public QObject, public PluginUiThread::Runner
{
public:
	static void install();
	static void uninstall();
	static bool waitServicing(QThread& thread, QDeadlineTimer deadline);
	bool isUiThread() const override;
	bool call(const std::function<void()>& work) override;
	bool post(std::function<void()> work) override;

private:
	struct Completion
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool done = false;
		std::exception_ptr error;
	};
	struct Task
	{
		std::function<void()> work;
		std::shared_ptr<Completion> completion;
	};
	bool enqueue(Task task);
	void drain();
	std::mutex mutex;
	std::deque<Task> queued;
	bool accepting = true;
	bool draining = false;
};
