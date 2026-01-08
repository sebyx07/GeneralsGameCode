/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// TheSuperHackers @feature Claude 08/01/2026 Thread pool unit tests (TDD)

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ThreadPool.h"
#include <atomic>
#include <vector>
#include <chrono>
#include <thread>

using namespace testing;

class ThreadPoolTest : public Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

// =============================================================================
// Construction Tests
// =============================================================================

TEST_F(ThreadPoolTest, ConstructWithDefaultThreadCount)
{
	ThreadPool pool;
	EXPECT_GT(pool.getThreadCount(), 0u);
}

TEST_F(ThreadPoolTest, ConstructWithSpecificThreadCount)
{
	ThreadPool pool(4);
	EXPECT_EQ(pool.getThreadCount(), 4u);
}

TEST_F(ThreadPoolTest, ConstructWithZeroThreadsUsesHardwareConcurrency)
{
	ThreadPool pool(0);
	EXPECT_GT(pool.getThreadCount(), 0u);
}

TEST_F(ThreadPoolTest, ConstructWithOneThread)
{
	ThreadPool pool(1);
	EXPECT_EQ(pool.getThreadCount(), 1u);
}

// =============================================================================
// Basic Job Submission Tests
// =============================================================================

TEST_F(ThreadPoolTest, SubmitSingleJob)
{
	ThreadPool pool(2);
	std::atomic<bool> executed{false};

	pool.submit([&executed]() {
		executed = true;
	});

	pool.waitForAll();
	EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, SubmitMultipleJobs)
{
	ThreadPool pool(4);
	std::atomic<int> counter{0};
	const int numJobs = 100;

	for (int i = 0; i < numJobs; ++i) {
		pool.submit([&counter]() {
			++counter;
		});
	}

	pool.waitForAll();
	EXPECT_EQ(counter.load(), numJobs);
}

TEST_F(ThreadPoolTest, SubmitJobsWithReturn)
{
	ThreadPool pool(2);

	auto future = pool.submitWithFuture([]() -> int {
		return 42;
	});

	EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, SubmitJobsWithReturnString)
{
	ThreadPool pool(2);

	auto future = pool.submitWithFuture([]() -> std::string {
		return "hello";
	});

	EXPECT_EQ(future.get(), "hello");
}

// =============================================================================
// Concurrency Tests
// =============================================================================

TEST_F(ThreadPoolTest, JobsExecuteConcurrently)
{
	ThreadPool pool(4);
	std::atomic<int> concurrentCount{0};
	std::atomic<int> maxConcurrent{0};
	std::atomic<int> completed{0};
	const int numJobs = 8;

	for (int i = 0; i < numJobs; ++i) {
		pool.submit([&]() {
			int current = ++concurrentCount;

			// Update max if needed
			int expected = maxConcurrent.load();
			while (current > expected && !maxConcurrent.compare_exchange_weak(expected, current)) {
				// Retry
			}

			// Simulate work
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

			--concurrentCount;
			++completed;
		});
	}

	pool.waitForAll();
	EXPECT_EQ(completed.load(), numJobs);
	EXPECT_GT(maxConcurrent.load(), 1) << "Jobs should run concurrently";
}

TEST_F(ThreadPoolTest, SingleThreadExecutesSequentially)
{
	ThreadPool pool(1);
	std::atomic<int> concurrentCount{0};
	std::atomic<int> maxConcurrent{0};
	const int numJobs = 5;

	for (int i = 0; i < numJobs; ++i) {
		pool.submit([&]() {
			int current = ++concurrentCount;

			int expected = maxConcurrent.load();
			while (current > expected && !maxConcurrent.compare_exchange_weak(expected, current)) {}

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			--concurrentCount;
		});
	}

	pool.waitForAll();
	EXPECT_EQ(maxConcurrent.load(), 1) << "Single thread should never have concurrent jobs";
}

// =============================================================================
// Wait and Synchronization Tests
// =============================================================================

TEST_F(ThreadPoolTest, WaitForAllBlocksUntilComplete)
{
	ThreadPool pool(2);
	std::atomic<bool> started{false};
	std::atomic<bool> finished{false};

	pool.submit([&]() {
		started = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		finished = true;
	});

	// Give job time to start
	while (!started) {
		std::this_thread::yield();
	}

	pool.waitForAll();
	EXPECT_TRUE(finished) << "waitForAll should block until job completes";
}

TEST_F(ThreadPoolTest, WaitForAllWithNoJobs)
{
	ThreadPool pool(2);
	pool.waitForAll(); // Should not hang
	SUCCEED();
}

TEST_F(ThreadPoolTest, MultipleWaitForAllCalls)
{
	ThreadPool pool(2);
	std::atomic<int> counter{0};

	pool.submit([&]() { ++counter; });
	pool.waitForAll();
	EXPECT_EQ(counter.load(), 1);

	pool.submit([&]() { ++counter; });
	pool.waitForAll();
	EXPECT_EQ(counter.load(), 2);
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST_F(ThreadPoolTest, ConcurrentSubmitFromMultipleThreads)
{
	ThreadPool pool(4);
	std::atomic<int> counter{0};
	const int jobsPerThread = 50;
	const int numSubmitters = 4;

	std::vector<std::thread> submitters;
	for (int t = 0; t < numSubmitters; ++t) {
		submitters.emplace_back([&]() {
			for (int i = 0; i < jobsPerThread; ++i) {
				pool.submit([&counter]() {
					++counter;
				});
			}
		});
	}

	for (auto& thread : submitters) {
		thread.join();
	}

	pool.waitForAll();
	EXPECT_EQ(counter.load(), jobsPerThread * numSubmitters);
}

// =============================================================================
// Shutdown Tests
// =============================================================================

TEST_F(ThreadPoolTest, DestructorWaitsForJobs)
{
	std::atomic<bool> jobCompleted{false};

	{
		ThreadPool pool(2);
		pool.submit([&]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			jobCompleted = true;
		});
	} // Destructor should wait

	EXPECT_TRUE(jobCompleted) << "Destructor should wait for pending jobs";
}

TEST_F(ThreadPoolTest, ShutdownPreventsNewJobs)
{
	ThreadPool pool(2);
	std::atomic<int> counter{0};

	pool.submit([&]() { ++counter; });
	pool.shutdown();

	// After shutdown, new jobs should be rejected
	pool.submit([&]() { ++counter; });

	pool.waitForAll();
	EXPECT_EQ(counter.load(), 1) << "Jobs after shutdown should be rejected";
}

TEST_F(ThreadPoolTest, IsRunningReturnsCorrectState)
{
	ThreadPool pool(2);
	EXPECT_TRUE(pool.isRunning());

	pool.shutdown();
	EXPECT_FALSE(pool.isRunning());
}

// =============================================================================
// Stress Tests
// =============================================================================

TEST_F(ThreadPoolTest, StressTestManySmallJobs)
{
	ThreadPool pool(4);
	std::atomic<int> counter{0};
	const int numJobs = 10000;

	for (int i = 0; i < numJobs; ++i) {
		pool.submit([&counter]() {
			++counter;
		});
	}

	pool.waitForAll();
	EXPECT_EQ(counter.load(), numJobs);
}

TEST_F(ThreadPoolTest, StressTestWithVaryingWorkloads)
{
	ThreadPool pool(4);
	std::atomic<int> completed{0};
	const int numJobs = 100;

	for (int i = 0; i < numJobs; ++i) {
		pool.submit([i, &completed]() {
			// Varying workload
			volatile int sum = 0;
			for (int j = 0; j < (i % 10 + 1) * 100; ++j) {
				sum += j;
			}
			(void)sum;
			++completed;
		});
	}

	pool.waitForAll();
	EXPECT_EQ(completed.load(), numJobs);
}

// =============================================================================
// Exception Safety Tests
// =============================================================================

TEST_F(ThreadPoolTest, JobExceptionDoesNotCrashPool)
{
	ThreadPool pool(2);
	std::atomic<int> counter{0};

	// Job that throws
	pool.submit([]() {
		throw std::runtime_error("Test exception");
	});

	// Jobs after exception should still work
	pool.submit([&counter]() {
		++counter;
	});

	pool.waitForAll();
	EXPECT_EQ(counter.load(), 1) << "Pool should continue working after exception";
}

// =============================================================================
// Pending Jobs Count Tests
// =============================================================================

TEST_F(ThreadPoolTest, GetPendingJobsCount)
{
	ThreadPool pool(1);
	std::atomic<bool> blockJob{true};

	// Submit blocking job
	pool.submit([&blockJob]() {
		while (blockJob) {
			std::this_thread::yield();
		}
	});

	// Wait for the blocking job to start
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	// Submit more jobs that will queue up
	for (int i = 0; i < 5; ++i) {
		pool.submit([]() {});
	}

	// Should have pending jobs
	EXPECT_GT(pool.getPendingJobsCount(), 0u);

	// Release blocking job
	blockJob = false;
	pool.waitForAll();

	EXPECT_EQ(pool.getPendingJobsCount(), 0u);
}
