/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#ifdef _MSC_VER
#pragma code_seg(push, "RT_CODE")
#endif

namespace eapo::rt
{
	// How many workers a call site's jobs should wake, learnt from the jobs
	// themselves. Owned by one call site and touched only by the thread that
	// calls FanOut::run for it.
	//
	// The only thing the governor trusts is how long the whole job took. It
	// holds a worker count, measures a window of jobs with it, tries one worker
	// more or one fewer for the next window, goes back for a third window, and
	// keeps the change only when the trial beat the windows on both sides of it
	// (so a machine that is getting busier or quieter does not decide the
	// outcome): one more worker has to make the job at
	// least five percent faster, one fewer may cost at most one percent, and a
	// run of removals may cost at most two percent in all. Time
	// spent inside the tasks is not a measure of benefit. On a processor whose
	// logical cores share a physical core, or whose memory bus is full, every
	// task takes longer as threads are added, so the threads look busy while
	// the job finishes no sooner.
	//
	// Total RAM is deliberately not an input. Workers process units in place and
	// allocate nothing per worker. What limits scaling is memory bandwidth, cache
	// pressure, slow cores and wake cost, and the job's own duration contains
	// all of them while a memory capacity number contains none.
	class FanOutGovernor
	{
	public:
		// Workers to wake for a job of `tasks` tasks on a team of `teamSize`
		// workers. Never more than tasks - 1 or teamSize.
		unsigned plan(size_t tasks, unsigned teamSize) noexcept
		{
			if (tasks < 2 || teamSize == 0)
			{
				planned_ = 0;
				return 0;
			}
			reach_ = static_cast<unsigned>((std::min)({tasks - 1,
				static_cast<size_t>(teamSize), static_cast<size_t>(maximumWorkers)}));
			if (settled_ > reach_)
			{
				// The team or the job is smaller than the count held so far.
				settled_ = reach_;
				target_ = reach_;
				restart(1);
			}
			planned_ = (std::min)(target_, wish());
			return planned_;
		}

		// What the job just measured: the summed time its tasks took (on all
		// threads), the wall time of the whole run() call, both in the same
		// ticks, how many workers were woken, and the ticks per second.
		void observe(uint64_t workTicks, uint64_t wallTicks, unsigned workersWoken,
			uint64_t ticksPerSecond) noexcept
		{
			if (wallTicks == 0 || ticksPerSecond == 0)
				return;

			const double workSeconds = static_cast<double>(workTicks)
				/ static_cast<double>(ticksPerSecond);
			if (hasWork_)
				smoothedWorkSeconds_ += smoothingWeight * (workSeconds - smoothedWorkSeconds_);
			else
			{
				smoothedWorkSeconds_ = workSeconds;
				hasWork_ = true;
			}

			// A job that ran inline because the team was busy says nothing about
			// the count being measured.
			if (workersWoken != planned_)
				return;
			if (planned_ != target_)
			{
				// The work is too light for the count held, so there is nothing
				// to compare. A trial in progress is abandoned.
				target_ = settled_;
				restart(1);
				return;
			}

			windowTicks_ += wallTicks;
			if (++windowJobs_ < windowLength)
				return;
			const double mean = static_cast<double>(windowTicks_) / windowLength;
			windowTicks_ = 0;
			windowJobs_ = 0;

			switch (phase_)
			{
			case Phase::rest:
				if (--restWindows_ == 0)
					phase_ = Phase::baseline;
				break;
			case Phase::baseline:
				baseline_ = mean;
				if (reference_ == 0.0)
					reference_ = mean;
				beginTrial();
				break;
			case Phase::trial:
				trialMean_ = mean;
				target_ = settled_;
				phase_ = Phase::confirm;
				break;
			case Phase::confirm:
				finishTrial(mean);
				break;
			}
		}

		// For diagnostics and tests: the count in use now (a trial included) and
		// the count last kept.
		unsigned ceiling() const noexcept
		{
			return target_;
		}

		unsigned settled() const noexcept
		{
			return settled_;
		}

		double smoothedWorkSeconds() const noexcept
		{
			return smoothedWorkSeconds_;
		}

		// Jobs in one measurement window. A multiple of every tail schedule the
		// partitioned convolver uses (4, 8, 16, 32 callbacks), so each window
		// holds the same mix of light and heavy blocks.
		static constexpr unsigned windowLength = 256;
		// Seven is the fixed capacity released before the governor existed.
		static constexpr unsigned initialWorkers = 7;
		// Sixty-three matches FanOut's admission-safe worker cap.
		static constexpr unsigned maximumWorkers = 63;
		// Windows to wait after both directions were refused; doubles up to this.
		static constexpr unsigned longestRest = 64;

	private:
		enum class Phase { rest, baseline, trial, confirm };

		// One eighth damps block-to-block timing jitter without reacting too slowly.
		static constexpr double smoothingWeight = 1.0 / 8.0;
		// Waking a worker needs at least 150 microseconds of measured task work.
		static constexpr double workerGrainSeconds = 150.0 / 1000000.0;
		// One more worker stays only when the job gets five percent faster.
		static constexpr double keepMoreBelow = 0.95;
		// One fewer worker stays unless the job gets more than one percent slower.
		static constexpr double keepFewerBelow = 1.01;
		// Removals stop once the job is two percent slower than it was when they
		// began. Without this, steps that each pass the one percent test add up.
		static constexpr double keepFewerBelowReference = 1.02;

		unsigned wish() const noexcept
		{
			if (!hasWork_)
				return maximumWorkers;
			const double workers = smoothedWorkSeconds_ / workerGrainSeconds;
			if (workers >= static_cast<double>(maximumWorkers))
				return maximumWorkers;
			if (workers <= 0.0)
				return 0;
			return static_cast<unsigned>(workers);
		}

		void restart(unsigned restWindows) noexcept
		{
			phase_ = Phase::rest;
			restWindows_ = restWindows;
			windowTicks_ = 0;
			windowJobs_ = 0;
		}

		void beginTrial() noexcept
		{
			const bool canAdd = settled_ < reach_;
			const bool canDrop = settled_ > 1;
			if (!canAdd && !canDrop)
			{
				restart(longestRest);
				return;
			}
			tryingMore_ = canAdd && (tryMoreNext_ || !canDrop);
			trialWorkers_ = tryingMore_ ? settled_ + 1 : settled_ - 1;
			target_ = trialWorkers_;
			phase_ = Phase::trial;
		}

		// `after` is the window measured with the held count once the trial ended.
		void finishTrial(double after) noexcept
		{
			const double around = (std::min)(baseline_, after);
			const bool keep = tryingMore_ ? trialMean_ < around * keepMoreBelow
				: trialMean_ < around * keepFewerBelow
					&& trialMean_ < reference_ * keepFewerBelowReference;
			if (keep)
			{
				// A count that earned its place is what later removals answer to.
				if (tryingMore_)
					reference_ = trialMean_;
				// Keep going the same way after one window of rest.
				settled_ = trialWorkers_;
				target_ = trialWorkers_;
				tryMoreNext_ = tryingMore_;
				refusals_ = 0;
				rest_ = 1;
				restart(1);
				return;
			}
			tryMoreNext_ = !tryingMore_;
			if (++refusals_ < 2)
			{
				// The other direction is tried next, against a fresh baseline.
				restart(1);
				return;
			}
			// Both directions were refused: this count is right for now. Look
			// again later, and less often each time it stays right.
			refusals_ = 0;
			restart(rest_);
			rest_ = (std::min)(rest_ * 2, longestRest);
		}

		double smoothedWorkSeconds_ = 0.0;
		double baseline_ = 0.0;
		double reference_ = 0.0;
		double trialMean_ = 0.0;
		uint64_t windowTicks_ = 0;
		unsigned windowJobs_ = 0;
		unsigned settled_ = initialWorkers;
		unsigned target_ = initialWorkers;
		unsigned planned_ = 0;
		unsigned trialWorkers_ = initialWorkers;
		unsigned reach_ = maximumWorkers;
		unsigned restWindows_ = 1;
		unsigned rest_ = 1;
		unsigned refusals_ = 0;
		Phase phase_ = Phase::rest;
		bool tryMoreNext_ = true;
		bool tryingMore_ = false;
		bool hasWork_ = false;
	};
}

#ifdef _MSC_VER
#pragma code_seg(pop)
#endif
