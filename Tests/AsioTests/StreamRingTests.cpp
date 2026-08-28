/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	StreamRing over one process: a heap region, unnamed events, and a
	consumer thread that plays the engine host. What the wrapper-daemon
	handoff must guarantee is pinned here, before any second process exists:
	readiness, in-order completion, Done/Late/Gone, slot reuse, the drop rule
	when the consumer falls two behind, and the pipelined shape.
*/

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "runtime/ipc/StreamRing.h"
#include "Tests/TestHarness.h"

using eapo::asio::Direction;
using eapo::asio::StreamFormat;
using eapo::ipc::RingConsumer;
using eapo::ipc::RingFault;
namespace RingGeometry = eapo::ipc::RingGeometry;
using eapo::ipc::RingProducer;
using eapo::ipc::RingState;
using eapo::ipc::RingSync;
using eapo::ipc::RingWait;

namespace
{
	test::Harness harness("StreamRingTests");

	struct Region
	{
		std::vector<unsigned char> bytes;
		HANDLE work[2] = {};
		HANDLE done[2] = {};
		HANDLE ready = nullptr;
		HANDLE consumerPeer = nullptr;   // signals "the producer died" to the consumer
		HANDLE producerPeer = nullptr;   // signals "the consumer died" to the producer

		explicit Region(const StreamFormat& format)
			: bytes(RingGeometry::totalBytes(format) + 64, 0xCD)
		{
			for (int i = 0; i < 2; i++)
			{
				work[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				done[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			}
			ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			consumerPeer = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			producerPeer = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		}

		~Region()
		{
			for (int i = 0; i < 2; i++)
			{
				CloseHandle(work[i]);
				CloseHandle(done[i]);
			}
			CloseHandle(ready);
			CloseHandle(consumerPeer);
			CloseHandle(producerPeer);
		}

		Region(const Region&) = delete;
		Region& operator=(const Region&) = delete;

		void* base()
		{
			// 64-byte aligned start inside the over-allocated vector.
			uintptr_t address = reinterpret_cast<uintptr_t>(bytes.data());
			address = (address + 63) & ~static_cast<uintptr_t>(63);
			return reinterpret_cast<void*>(address);
		}

		RingSync producerSync() const
		{
			RingSync sync;
			sync.work[0] = work[0];
			sync.work[1] = work[1];
			sync.done[0] = done[0];
			sync.done[1] = done[1];
			sync.ready = ready;
			sync.peer = producerPeer;
			return sync;
		}

		RingSync consumerSync() const
		{
			RingSync sync = producerSync();
			sync.peer = consumerPeer;
			return sync;
		}
	};

	StreamFormat stereoFormat(uint32_t frames = 64, uint32_t inputs = 2)
	{
		StreamFormat format;
		format.sampleRate = 48000.0;
		format.frames = frames;
		format.channels[0] = 2;
		format.channels[1] = inputs;
		return format;
	}

	// The engine host stand-in: multiplies every sample by `gain`, sleeps
	// `delayUs` per block when asked, and dies (leaves without releasing)
	// at `dieAtSequence`.
	struct ConsumerThread
	{
		Region& region;
		float gain = 0.5f;
		std::atomic<uint32_t> delayUs{0};
		std::atomic<uint32_t> dieAtSequence{0};
		std::atomic<uint32_t> served{0};
		std::atomic<bool> finished{false};
		std::vector<uint32_t> order;
		std::thread thread;

		explicit ConsumerThread(Region& region)
			: region(region)
		{
		}

		void start()
		{
			thread = std::thread([this] {
				RingConsumer consumer(region.base(), region.bytes.size(), region.consumerSync());
				if (!consumer.valid())
				{
					consumer.setState(RingState::Fault, RingFault::LayoutMismatch);
					finished = true;
					return;
				}
				consumer.setConsumerPid(4242);
				consumer.setState(RingState::Ready);
				RingConsumer::Acquired acquired;
				while (consumer.acquire(acquired, 2000))
				{
					if (dieAtSequence != 0 && acquired.sequence == dieAtSequence)
					{
						SetEvent(region.producerPeer);   // the "process" is gone
						finished = true;
						return;
					}
					if (delayUs != 0)
						std::this_thread::sleep_for(std::chrono::microseconds(delayUs.load()));
					const StreamFormat& format = consumer.format();
					const size_t samples = static_cast<size_t>(format.channelCount(acquired.direction)) * format.frames;
					for (size_t i = 0; i < samples; i++)
						acquired.slot[i] *= gain;
					order.push_back(acquired.sequence);
					consumer.release(acquired);
					served++;
				}
				finished = true;
			});
		}

		void join()
		{
			if (thread.joinable())
				thread.join();
		}
	};

	void fillSlot(float* slot, size_t samples, float value)
	{
		for (size_t i = 0; i < samples; i++)
			slot[i] = value + static_cast<float>(i) * 0.001f;
	}

	bool slotIs(const float* slot, size_t samples, float value, float gain)
	{
		for (size_t i = 0; i < samples; i++)
		{
			const float expected = (value + static_cast<float>(i) * 0.001f) * gain;
			if (std::fabs(slot[i] - expected) > 1e-5f)
				return false;
		}
		return true;
	}

	void testGeometry()
	{
		StreamFormat format = stereoFormat(64, 0);
		harness.expectEqual(RingGeometry::slotBytes(format, Direction::Output), 512u, "2 x 64 floats round to 512 bytes");
		harness.expectEqual(RingGeometry::slotBytes(format, Direction::Input), 0u, "a disabled direction has no slot");
		harness.expectEqual(RingGeometry::totalBytes(format), 512u + 1024u, "header plus two output slots");
		format.channels[1] = 3;
		format.frames = 7;
		harness.expectEqual(RingGeometry::slotBytes(format, Direction::Input), 128u, "3 x 7 floats (84 bytes) round up to 128");
		harness.expectEqual(sizeof(eapo::ipc::RingHeader), eapo::ipc::ringHeaderBytes, "the header is 512 bytes");
	}

	void testReadinessAndInOrderService()
	{
		StreamFormat format = stereoFormat();
		Region region(format);
		RingProducer producer(region.base(), format, 1111, region.producerSync());
		harness.expect(producer.state() == RingState::Announced, "the producer announces");
		harness.expect(producer.waitReady(50) == RingState::Announced, "no consumer: waitReady times out on Announced");

		ConsumerThread consumer(region);
		consumer.start();
		harness.expect(producer.waitReady(5000) == RingState::Ready, "the consumer becomes Ready");
		harness.expectEqual(producer.consumerPid(), 4242u, "the consumer wrote its pid");

		const size_t samples = 2 * 64;
		for (uint32_t seq = 1; seq <= 5; seq++)
		{
			harness.require(producer.canPublish(Direction::Output, seq), "slot free for seq " + std::to_string(seq));
			fillSlot(producer.slot(Direction::Output, seq), samples, static_cast<float>(seq));
			producer.publish(Direction::Output, seq);
			harness.expect(producer.wait(Direction::Output, seq, 1000000) == RingWait::Done, "seq " + std::to_string(seq) + " is Done");
			harness.expect(slotIs(producer.slot(Direction::Output, seq), samples, static_cast<float>(seq), 0.5f), "seq " + std::to_string(seq) + " came back processed");
		}
		fillSlot(producer.slot(Direction::Input, 1), samples, 9.0f);
		producer.publish(Direction::Input, 1);
		harness.expect(producer.wait(Direction::Input, 1, 1000000) == RingWait::Done, "the input lane is served too");
		harness.expect(slotIs(producer.slot(Direction::Input, 1), samples, 9.0f, 0.5f), "the input block came back processed");

		producer.close();
		consumer.join();
		harness.expectEqual(consumer.served.load(), 6u, "six blocks were served");
		harness.expect(consumer.order == std::vector<uint32_t>({1, 2, 3, 4, 5, 1}), "blocks were served in publication order");
	}

	void testLateThenCatchUp()
	{
		StreamFormat format = stereoFormat(64, 0);
		Region region(format);
		RingProducer producer(region.base(), format, 1, region.producerSync());
		ConsumerThread consumer(region);
		consumer.delayUs = 20000;
		consumer.start();
		harness.require(producer.waitReady(5000) == RingState::Ready, "ready");

		const size_t samples = 2 * 64;
		fillSlot(producer.slot(Direction::Output, 1), samples, 1.0f);
		producer.publish(Direction::Output, 1);
		harness.expect(producer.wait(Direction::Output, 1, 500) == RingWait::Late, "a 20 ms consumer misses a 500 us budget");
		harness.expect(producer.wait(Direction::Output, 1, 0) == RingWait::Late, "a zero budget answers at once");
		// Seq 2 uses the other slot: publishable while seq 1 is still in flight.
		harness.expect(producer.canPublish(Direction::Output, 2), "seq 2 may be published while seq 1 is late");
		fillSlot(producer.slot(Direction::Output, 2), samples, 2.0f);
		producer.publish(Direction::Output, 2);
		// Seq 3 would reuse slot 1, which the consumer may still be reading.
		harness.expectFalse(producer.canPublish(Direction::Output, 3), "seq 3 must wait for seq 1 to complete");
		harness.expect(producer.wait(Direction::Output, 2, 2000000) == RingWait::Done, "seq 2 completes once the consumer catches up");
		harness.expect(producer.completed(Direction::Output, 1), "seq 1 was completed too, in order");
		harness.expect(producer.canPublish(Direction::Output, 3), "seq 3 is publishable now");
		harness.expect(slotIs(producer.slot(Direction::Output, 1), samples, 1.0f, 0.5f), "the late block was still processed");
		harness.expect(slotIs(producer.slot(Direction::Output, 2), samples, 2.0f, 0.5f), "and the next one");

		producer.close();
		consumer.join();
		harness.expect(consumer.order == std::vector<uint32_t>({1, 2}), "the consumer never skipped the late block");
	}

	void testGoneWhenTheConsumerDies()
	{
		StreamFormat format = stereoFormat(32, 0);
		Region region(format);
		RingProducer producer(region.base(), format, 1, region.producerSync());
		ConsumerThread consumer(region);
		consumer.dieAtSequence = 2;
		consumer.start();
		harness.require(producer.waitReady(5000) == RingState::Ready, "ready");

		producer.publish(Direction::Output, 1);
		harness.expect(producer.wait(Direction::Output, 1, 1000000) == RingWait::Done, "seq 1 is served");
		producer.publish(Direction::Output, 2);
		harness.expect(producer.wait(Direction::Output, 2, 5000000) == RingWait::Gone, "the peer handle turns the wait into Gone");
		producer.close();
		consumer.join();
	}

	void testFaultAndClosingReachTheProducer()
	{
		{
			StreamFormat format = stereoFormat(16, 0);
			Region region(format);
			RingProducer producer(region.base(), format, 1, region.producerSync());
			// Corrupt the magic so the consumer refuses the layout.
			static_cast<eapo::ipc::RingHeader*>(region.base())->layoutVersion = 99;
			ConsumerThread consumer(region);
			consumer.start();
			harness.expect(producer.waitReady(5000) == RingState::Fault, "a layout mismatch is reported as Fault");
			harness.expect(producer.fault() == RingFault::LayoutMismatch, "with the mismatch code");
			consumer.join();
		}
		{
			StreamFormat format = stereoFormat(16, 0);
			Region region(format);
			RingProducer producer(region.base(), format, 1, region.producerSync());
			ConsumerThread consumer(region);
			consumer.start();
			harness.require(producer.waitReady(5000) == RingState::Ready, "ready");
			producer.close();
			consumer.join();
			harness.expect(consumer.finished.load(), "close() wakes a consumer that was waiting for work");
			harness.expect(producer.wait(Direction::Output, 1, 100) == RingWait::Gone, "waiting after close is Gone");
		}
	}

	void testConsumerSeesProducerDeath()
	{
		StreamFormat format = stereoFormat(16, 0);
		Region region(format);
		RingProducer producer(region.base(), format, 1, region.producerSync());
		ConsumerThread consumer(region);
		consumer.start();
		harness.require(producer.waitReady(5000) == RingState::Ready, "ready");
		producer.publish(Direction::Output, 1);
		harness.expect(producer.wait(Direction::Output, 1, 1000000) == RingWait::Done, "seq 1 served");
		SetEvent(region.consumerPeer);       // the DAW process vanished
		consumer.join();
		harness.expect(consumer.finished.load(), "the consumer leaves when the producer's process handle signals");
	}

	void testPipelinedShape()
	{
		// The pipelined adapter publishes seq and then waits for seq - 1: the
		// wait is on an older sequence that is usually already complete.
		StreamFormat format = stereoFormat(64, 0);
		Region region(format);
		RingProducer producer(region.base(), format, 1, region.producerSync());
		ConsumerThread consumer(region);
		consumer.start();
		harness.require(producer.waitReady(5000) == RingState::Ready, "ready");
		const size_t samples = 2 * 64;
		bool allDone = true;
		for (uint32_t seq = 1; seq <= 50; seq++)
		{
			while (!producer.canPublish(Direction::Output, seq))
				std::this_thread::yield();
			fillSlot(producer.slot(Direction::Output, seq), samples, static_cast<float>(seq));
			producer.publish(Direction::Output, seq);
			if (seq > 1)
			{
				allDone = allDone && producer.wait(Direction::Output, seq - 1, 1000000) == RingWait::Done;
				allDone = allDone && slotIs(producer.slot(Direction::Output, seq - 1), samples, static_cast<float>(seq - 1), 0.5f);
			}
		}
		harness.expect(allDone, "every previous block was complete and processed by the time the next was published");
		// The adapter never waits for the block published last; the consumer
		// drops it when Closing arrives first. Wait for it here so the count
		// below does not depend on the consumer thread's scheduling.
		harness.expect(producer.wait(Direction::Output, 50, 1000000) == RingWait::Done, "the last block completes when asked for");
		producer.close();
		consumer.join();
		harness.expectEqual(consumer.served.load(), 50u, "all 50 blocks served");
	}
}

int runStreamRingTests()
{
	testGeometry();
	testReadinessAndInOrderService();
	testLateThenCatchUp();
	testGoneWhenTheConsumerDies();
	testFaultAndClosingReachTheProducer();
	testConsumerSeesProducerDeath();
	testPipelinedShape();
	harness.report();
	return 0;
}
