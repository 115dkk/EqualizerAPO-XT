/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <vector>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSemaphore>
#include <QThread>

#include "Editor/helpers/PluginUiThreadBridge.h"
#include "vst/VSTPluginInstance.h"
#include "../TestHarness.h"
#include "EditorLogicTestSupport.h"

namespace
{
class BridgeSession
{
public:
	BridgeSession()
	{
		requireTrue(QCoreApplication::instance() != nullptr && QThread::isMainThread(),
			"bridge tests use the suite's QCoreApplication on its main thread");
		PluginUiThreadBridge::install();
	}
	~BridgeSession() { PluginUiThreadBridge::uninstall(); }
	BridgeSession(const BridgeSession&) = delete;
	BridgeSession& operator=(const BridgeSession&) = delete;
};

class BridgeWorker final : public QThread
{
public:
	explicit BridgeWorker(std::function<void()> action) : action(std::move(action)) {}
	~BridgeWorker() override
	{
		// Failure cleanup does not rely on the waitServicing implementation
		// under test. Captured state and BridgeSession outlive this worker.
		QDeadlineTimer deadline(5000);
		while (!wait(0) && !deadline.hasExpired())
		{
			QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
			QThread::msleep(1);
		}
		if (!wait(0))
			qFatal("PluginUiThreadBridge test worker did not finish during cleanup");
	}
protected:
	void run() override { action(); }
private:
	std::function<void()> action;
};

struct RecordedWork
{
	std::atomic<bool> accepted{false};
	std::atomic<Qt::HANDLE> thread{nullptr};
};

// No plug-in DLL, audio or window: only the real instance deleter is used.
// Its destruction callback lets tests observe post() on the installed bridge
// without exposing the bridge's private singleton as a new product API.
class RetirementMarker final : public VSTPluginInstance
{
public:
	explicit RetirementMarker(std::function<void()> retired)
		: VSTPluginInstance({}, ProcessLevel::User), retired(std::move(retired)) {}
	~RetirementMarker() override { retired(); }
	int numInputs() const override { return 0; }
	int numOutputs() const override { return 0; }
	std::optional<VST3BusLayout> getNegotiatedVST3InputLayout() const override { return {}; }
	std::optional<VST3BusLayout> getNegotiatedVST3OutputLayout() const override { return {}; }
	bool canProcessNow() const override { return false; }
	int uniqueID() const override { return 0; }
	std::wstring getName() const override { return L"retirement marker"; }
	int getInitialDelay() const override { return 0; }
	StateRestoreResult writeToEffect(const std::wstring&, const std::unordered_map<std::wstring, float>&) override { return {}; }
	void readFromEffect(std::wstring&, std::unordered_map<std::wstring, float>&) const override {}
	bool startProcessing() override { return false; }
	void stopProcessing() override {}
	void doIdle() override {}
	void stopEditing() override {}
	void onSizeWindow(int, int) override {}
protected:
	bool initializeImpl() override { return true; }
	bool prepareProcessingImpl(float, int) override { return false; }
	bool negotiateChannelCount(int, const std::vector<std::wstring>&) override { return false; }
	bool negotiateBusChannelCounts(int, int, const std::vector<std::wstring>&,
		const std::vector<std::wstring>&) override { return false; }
	bool negotiateBusLayouts(VST3BusLayout, VST3BusLayout, int, const std::vector<std::wstring>&,
		const std::vector<std::wstring>&) override { return false; }
	void processDoubleReplacing(double**, double**, int) override {}
	void processReplacing(float**, float**, int) override {}
	void process(float**, float**, int) override {}
	bool startEditingImpl(HWND, short*, short*, double) override { return false; }
	bool rescaleEditorImpl(double) override { return false; }
private:
	std::function<void()> retired;
};

void postRetirement(std::function<void()> work)
{
	VSTPluginInstancePtr marker(new RetirementMarker(std::move(work)));
	marker.reset();
}
}

TEST_CASE(EditorLogicTests, testPluginUiBridgeWaitServicingRunsParkedWorker)
{
	const auto mainThread = QThread::currentThreadId();
	RecordedWork result;
	QSemaphore entered;
	BridgeSession bridge;
	BridgeWorker worker([&] {
		entered.release();
		result.accepted = PluginUiThread::run([&] { result.thread = QThread::currentThreadId(); });
	});
	worker.start();
	requireTrue(entered.tryAcquire(1, 2000), "worker reaches its synchronous UI call");
	QElapsedTimer clock;
	clock.start();
	const bool finished = PluginUiThreadBridge::waitServicing(worker, QDeadlineTimer(5000));
	const qint64 elapsed = clock.elapsed();
	std::printf("PluginUiBridge serviced wait: finished=%d elapsed=%lld ms mainThread=%d\n",
		finished, static_cast<long long>(elapsed), result.thread.load() == mainThread);
	expectTrue(finished, "serviced wait finishes without a running event loop");
	expectTrue(elapsed < 2000, "serviced wait completes well before its five-second deadline");
	expectTrue(result.accepted.load(), "the bridge accepts the worker call");
	expectTrue(result.thread.load() == mainThread, "the worker's work executes on the main thread");
}

TEST_CASE(EditorLogicTests, testPluginUiBridgePlainWaitIsTheNegativeControl)
{
	const auto mainThread = QThread::currentThreadId();
	RecordedWork result;
	QSemaphore entered;
	BridgeSession bridge;
	BridgeWorker worker([&] {
		entered.release();
		result.accepted = PluginUiThread::run([&] { result.thread = QThread::currentThreadId(); });
	});
	worker.start();
	requireTrue(entered.tryAcquire(1, 2000), "negative-control worker reaches its UI call");
	QElapsedTimer clock;
	clock.start();
	const bool plainFinished = worker.wait(300);
	const qint64 elapsed = clock.elapsed();
	const bool workBeforeService = result.thread.load() != nullptr;
	const bool serviced = PluginUiThreadBridge::waitServicing(worker, QDeadlineTimer(5000));
	std::printf("PluginUiBridge NEGATIVE plain wait: finished=%d elapsed=%lld ms workBeforeService=%d; serviced=%d mainThread=%d\n",
		plainFinished, static_cast<long long>(elapsed), workBeforeService, serviced, result.thread.load() == mainThread);
	expectFalse(plainFinished, "the old plain wait cannot finish a worker parked on UI work");
	expectFalse(workBeforeService, "plain wait does not dispatch the queued UI work");
	expectTrue(elapsed >= 250 && elapsed < 2000, "negative control waits for its bounded timeout");
	expectTrue(serviced && result.accepted.load(), "servicing then releases the parked worker");
	expectTrue(result.thread.load() == mainThread, "the recovered call still runs on the main thread");
}

TEST_CASE(EditorLogicTests, testPluginUiBridgeQueuedInvocationWakesEventProcessing)
{
	const auto mainThread = QThread::currentThreadId();
	RecordedWork result;
	QSemaphore entered;
	BridgeSession bridge;
	BridgeWorker worker([&] {
		entered.release();
		result.accepted = PluginUiThread::run([&] { result.thread = QThread::currentThreadId(); });
	});
	worker.start();
	requireTrue(entered.tryAcquire(1, 2000), "event-loop worker reaches its UI call");
	QDeadlineTimer deadline(2000);
	QElapsedTimer clock;
	clock.start();
	while (!worker.isFinished() && !deadline.hasExpired())
	{
		// Deliberately no waitServicing: only the queued invocation can drain.
		QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(1);
	}
	const bool finished = worker.isFinished();
	std::printf("PluginUiBridge event wake: finished=%d elapsed=%lld ms mainThread=%d\n",
		finished, static_cast<long long>(clock.elapsed()), result.thread.load() == mainThread);
	expectTrue(finished && result.accepted.load(), "processEvents dispatches the queued bridge wake-up");
	expectTrue(result.thread.load() == mainThread, "queued wake-up runs work on the main thread");
}

TEST_CASE(EditorLogicTests, testPluginUiBridgeRetireCallRetireShareFifo)
{
	const auto mainThread = QThread::currentThreadId();
	std::mutex orderMutex;
	std::vector<char> order;
	bool allOnMain = true;
	std::atomic<bool> accepted{false};
	const auto record = [&](char value) {
		std::lock_guard<std::mutex> lock(orderMutex);
		order.push_back(value);
		allOnMain = allOnMain && QThread::currentThreadId() == mainThread;
	};
	BridgeSession bridge;
	BridgeWorker worker([&] {
		postRetirement([&] { record('A'); });
		accepted = PluginUiThread::run([&] { record('B'); });
		postRetirement([&] { record('C'); });
	});
	worker.start();
	const bool finished = PluginUiThreadBridge::waitServicing(worker, QDeadlineTimer(5000));
	std::lock_guard<std::mutex> lock(orderMutex);
	const bool fifo = order == std::vector<char>({'A', 'B', 'C'});
	std::printf("PluginUiBridge FIFO retirement: finished=%d orderABC=%d mainThread=%d\n", finished, fifo, allOnMain);
	expectTrue(finished && accepted.load(), "worker finishes after posting both retirements");
	expectTrue(fifo, "retirement A, synchronous B and retirement C retain FIFO order");
	expectTrue(allOnMain, "both deferred destructors and synchronous work run on the main thread");
}

TEST_CASE(EditorLogicTests, testPluginUiBridgeUninstallDrainsAndRestoresInlineCalls)
{
	const auto mainThread = QThread::currentThreadId();
	std::atomic<Qt::HANDLE> retiredOn{nullptr};
	RecordedWork inlineResult;
	std::atomic<Qt::HANDLE> workerThread{nullptr};
	BridgeSession bridge;
	BridgeWorker poster([&] { postRetirement([&] { retiredOn = QThread::currentThreadId(); }); });
	poster.start();
	requireTrue(poster.wait(2000), "posting a retirement does not block the worker");
	const bool ranBeforeUninstall = retiredOn.load() != nullptr;
	PluginUiThreadBridge::uninstall();
	const bool drainedOnMain = retiredOn.load() == mainThread;
	BridgeWorker caller([&] {
		workerThread = QThread::currentThreadId();
		inlineResult.accepted = PluginUiThread::run([&] { inlineResult.thread = QThread::currentThreadId(); });
	});
	caller.start();
	const bool finished = caller.wait(2000);
	const bool ranInline = inlineResult.thread.load() == workerThread.load() && workerThread.load() != mainThread;
	std::printf("PluginUiBridge uninstall: before=%d drainedOnMain=%d inlineFinished=%d inlineOnWorker=%d\n",
		ranBeforeUninstall, drainedOnMain, finished, ranInline);
	expectFalse(ranBeforeUninstall, "unserviced retirement remains queued before uninstall");
	expectTrue(drainedOnMain, "uninstall executes the pending retirement before returning");
	expectTrue(finished && inlineResult.accepted.load(), "run accepts work without the uninstalled runner");
	expectTrue(ranInline, "after uninstall the worker runs its own work inline");
}

TEST_CASE(EditorLogicTests, testPluginUiBridgeWaitServicingHonorsDeadline)
{
	QSemaphore sleeping;
	BridgeSession bridge;
	BridgeWorker worker([&] {
		sleeping.release();
		QThread::msleep(600);
	});
	worker.start();
	requireTrue(sleeping.tryAcquire(1, 2000), "deadline-control worker starts sleeping");
	QElapsedTimer clock;
	clock.start();
	const bool finished = PluginUiThreadBridge::waitServicing(worker, QDeadlineTimer(150));
	const qint64 elapsed = clock.elapsed();
	const bool joined = worker.wait(2000);
	std::printf("PluginUiBridge deadline: finished=%d elapsed=%lld ms joined=%d\n",
		finished, static_cast<long long>(elapsed), joined);
	expectFalse(finished, "150 ms deadline expires before the 600 ms worker completes");
	expectTrue(elapsed >= 100 && elapsed < 450, "serviced wait respects the bounded deadline");
	expectTrue(joined, "the sleeping worker is joined before the test returns");
}
