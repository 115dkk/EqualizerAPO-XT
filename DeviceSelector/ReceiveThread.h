/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <list>
#include <winsock2.h>

#include "runtime/errors/WideError.h"

class ReceiveException : public WideError
{
public:
	ReceiveException()
		: WideError(std::wstring())
	{
	}

	ReceiveException(const std::wstring& message)
		: WideError(message)
	{
	}

	bool isEmpty() const
	{
		return getMessage().empty();
	}
};

class ReceiveThread
{
public:
	ReceiveThread(const std::wstring& pipeName);
	~ReceiveThread();
	template<class Clock, class Duration> std::string waitUntil(const std::chrono::time_point<Clock, Duration>& time)
	{
		std::unique_lock lock(mutex);
		while (answers.empty() && caughtException.isEmpty())
		{
			if (cond.wait_until(lock, time) == std::cv_status::timeout)
				break;
		}

		if (!caughtException.isEmpty())
			throw caughtException;

		std::string answer;
		if (!answers.empty())
		{
			answer = answers.front();
			answers.pop_front();
		}

		return answer;
	}

	void stop();

private:
	void run();

	const std::wstring pipeName;
	std::thread thread;
	std::list<std::string> answers;
	std::mutex mutex;
	std::condition_variable cond;
	ReceiveException caughtException;
};
