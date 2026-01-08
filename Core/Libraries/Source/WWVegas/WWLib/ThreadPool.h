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

// TheSuperHackers @feature Claude 08/01/2026 Thread pool for parallel job execution

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * A high-performance thread pool for parallel job execution.
 *
 * Design principles:
 * - Simple interface: submit jobs, wait for completion
 * - Exception safety: job exceptions don't crash the pool
 * - Graceful shutdown: waits for pending jobs on destruction
 * - Thread-safe: submit from any thread
 *
 * Usage:
 *   ThreadPool pool(4);  // Create pool with 4 worker threads
 *
 *   // Fire-and-forget job
 *   pool.submit([]() { doWork(); });
 *
 *   // Job with return value
 *   auto future = pool.submitWithFuture([]() { return computeResult(); });
 *   int result = future.get();
 *
 *   // Wait for all pending jobs
 *   pool.waitForAll();
 */
class ThreadPool
{
public:
	/**
	 * Creates a thread pool with the specified number of worker threads.
	 * If threadCount is 0, uses hardware_concurrency().
	 */
	explicit ThreadPool(unsigned int threadCount = 0);

	/**
	 * Destroys the thread pool.
	 * Waits for all pending jobs to complete before returning.
	 */
	~ThreadPool();

	// Non-copyable, non-movable
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	ThreadPool(ThreadPool&&) = delete;
	ThreadPool& operator=(ThreadPool&&) = delete;

	/**
	 * Submits a job for execution. Fire-and-forget style.
	 * If the pool is shut down, the job is silently discarded.
	 */
	template<typename Func>
	void submit(Func&& func);

	/**
	 * Submits a job and returns a future for its result.
	 * Use future.get() to retrieve the result.
	 */
	template<typename Func>
	auto submitWithFuture(Func&& func) -> std::future<decltype(func())>;

	/**
	 * Blocks until all pending jobs have completed.
	 * New jobs can still be submitted while waiting.
	 */
	void waitForAll();

	/**
	 * Initiates graceful shutdown.
	 * Pending jobs will complete, but new submissions are rejected.
	 */
	void shutdown();

	/**
	 * Returns true if the pool is running (not shut down).
	 */
	bool isRunning() const;

	/**
	 * Returns the number of worker threads.
	 */
	unsigned int getThreadCount() const;

	/**
	 * Returns approximate count of pending jobs (including running).
	 */
	unsigned int getPendingJobsCount() const;

private:
	void workerThread();
	void submitJob(std::function<void()> job);

	std::vector<std::thread> m_workers;
	std::queue<std::function<void()>> m_jobQueue;

	mutable std::mutex m_queueMutex;
	std::condition_variable m_jobAvailable;
	std::condition_variable m_jobComplete;

	std::atomic<bool> m_running{true};
	std::atomic<unsigned int> m_pendingJobs{0};
	unsigned int m_threadCount{0};
};

// =============================================================================
// Template Implementation
// =============================================================================

template<typename Func>
void ThreadPool::submit(Func&& func)
{
	if (!m_running) {
		return;
	}

	submitJob(std::function<void()>(std::forward<Func>(func)));
}

template<typename Func>
auto ThreadPool::submitWithFuture(Func&& func) -> std::future<decltype(func())>
{
	using ReturnType = decltype(func());

	auto task = std::make_shared<std::packaged_task<ReturnType()>>(
		std::forward<Func>(func)
	);

	std::future<ReturnType> future = task->get_future();

	if (m_running) {
		submitJob([task]() { (*task)(); });
	}

	return future;
}

// =============================================================================
// Inline Implementation
// =============================================================================

inline ThreadPool::ThreadPool(unsigned int threadCount)
{
	if (threadCount == 0) {
		threadCount = std::thread::hardware_concurrency();
		if (threadCount == 0) {
			threadCount = 1; // Fallback if hardware_concurrency returns 0
		}
	}

	m_threadCount = threadCount;
	m_workers.reserve(threadCount);

	for (unsigned int i = 0; i < threadCount; ++i) {
		m_workers.emplace_back(&ThreadPool::workerThread, this);
	}
}

inline ThreadPool::~ThreadPool()
{
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		m_running = false;
	}

	m_jobAvailable.notify_all();

	for (auto& worker : m_workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}

inline void ThreadPool::workerThread()
{
	while (true) {
		std::function<void()> job;

		{
			std::unique_lock<std::mutex> lock(m_queueMutex);

			m_jobAvailable.wait(lock, [this]() {
				return !m_running || !m_jobQueue.empty();
			});

			if (!m_running && m_jobQueue.empty()) {
				return;
			}

			if (!m_jobQueue.empty()) {
				job = std::move(m_jobQueue.front());
				m_jobQueue.pop();
			}
		}

		if (job) {
			try {
				job();
			} catch (...) {
				// Swallow exceptions to prevent pool crash
				// In a production system, you might want to log this
			}

			--m_pendingJobs;
			m_jobComplete.notify_all();
		}
	}
}

inline void ThreadPool::submitJob(std::function<void()> job)
{
	++m_pendingJobs;

	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		m_jobQueue.push(std::move(job));
	}

	m_jobAvailable.notify_one();
}

inline void ThreadPool::waitForAll()
{
	std::unique_lock<std::mutex> lock(m_queueMutex);
	m_jobComplete.wait(lock, [this]() {
		return m_pendingJobs == 0;
	});
}

inline void ThreadPool::shutdown()
{
	m_running = false;
	m_jobAvailable.notify_all();
}

inline bool ThreadPool::isRunning() const
{
	return m_running;
}

inline unsigned int ThreadPool::getThreadCount() const
{
	return m_threadCount;
}

inline unsigned int ThreadPool::getPendingJobsCount() const
{
	return m_pendingJobs;
}
