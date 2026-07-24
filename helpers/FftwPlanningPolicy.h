#pragma once

#include <mutex>

class FftwPlanningPolicy
{
public:
	class Session
	{
	public:
		Session();
		unsigned flags() const;
		bool exportWisdomForLength(int transformLength);

	private:
		std::unique_lock<std::mutex> plannerLock;
	};
};
