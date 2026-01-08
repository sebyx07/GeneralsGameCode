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

// TheSuperHackers @feature Claude 08/01/2026 Parallel execution utilities

#pragma once

#include "ThreadPool.h"
#include <algorithm>
#include <iterator>
#include <vector>

/**
 * Returns a reference to the global thread pool singleton.
 * The pool is lazily initialized on first access.
 */
inline ThreadPool& getGlobalThreadPool()
{
	static ThreadPool instance;
	return instance;
}

/**
 * Executes a function in parallel over a range of indices [begin, end).
 *
 * Each index in the range is processed exactly once by exactly one thread.
 * The order of execution is undefined, but all indices are guaranteed to be
 * processed before the function returns.
 *
 * For deterministic results (important for replay compatibility):
 * - Each index should operate on independent data
 * - Use atomic operations or separate output locations per index
 *
 * @param begin     Start index (inclusive)
 * @param end       End index (exclusive)
 * @param func      Function taking an int index
 * @param chunkSize Number of indices per job (default: auto-calculated)
 * @param pool      Thread pool to use (default: global pool)
 *
 * Example:
 *   std::vector<int> data(1000);
 *   parallel_for(0, 1000, [&data](int i) {
 *       data[i] = i * 2;
 *   });
 */
template<typename Func>
void parallel_for(int begin, int end, Func&& func, int chunkSize = 0, ThreadPool* pool = nullptr)
{
	if (begin >= end) {
		return;
	}

	if (pool == nullptr) {
		pool = &getGlobalThreadPool();
	}

	const int totalItems = end - begin;

	// Auto-calculate chunk size if not specified
	if (chunkSize <= 0) {
		const int threadCount = static_cast<int>(pool->getThreadCount());
		// Aim for roughly 4 chunks per thread for load balancing
		chunkSize = std::max(1, totalItems / (threadCount * 4));
	}

	// Calculate number of chunks
	const int numChunks = (totalItems + chunkSize - 1) / chunkSize;

	// Submit all chunks as jobs
	std::atomic<int> completedChunks{0};

	for (int chunk = 0; chunk < numChunks; ++chunk) {
		const int chunkBegin = begin + chunk * chunkSize;
		const int chunkEnd = std::min(chunkBegin + chunkSize, end);

		pool->submit([chunkBegin, chunkEnd, &func, &completedChunks]() {
			for (int i = chunkBegin; i < chunkEnd; ++i) {
				func(i);
			}
			++completedChunks;
		});
	}

	// Wait for all chunks to complete
	pool->waitForAll();
}

/**
 * Executes a function in parallel over a range of iterators.
 *
 * Similar to std::for_each, but executed in parallel.
 *
 * @param begin Iterator to first element
 * @param end   Iterator past last element
 * @param func  Function taking element reference
 *
 * Example:
 *   std::vector<int> data{1, 2, 3, 4, 5};
 *   parallel_for_each(data.begin(), data.end(), [](int& x) {
 *       x *= 2;
 *   });
 */
template<typename Iterator, typename Func>
void parallel_for_each(Iterator begin, Iterator end, Func&& func)
{
	const auto distance = std::distance(begin, end);
	if (distance <= 0) {
		return;
	}

	// Convert to index-based parallel_for
	std::vector<typename std::iterator_traits<Iterator>::pointer> ptrs;
	ptrs.reserve(static_cast<size_t>(distance));

	for (auto it = begin; it != end; ++it) {
		ptrs.push_back(&(*it));
	}

	parallel_for(0, static_cast<int>(ptrs.size()), [&ptrs, &func](int i) {
		func(*ptrs[i]);
	});
}

/**
 * Performs a parallel reduction over a range of indices.
 *
 * Each index is transformed by mapFunc, then all results are combined
 * using reduceFunc. The reduction is performed in a tree-like manner
 * for efficiency.
 *
 * Note: reduceFunc must be associative and commutative for correctness.
 *
 * @param begin      Start index (inclusive)
 * @param end        End index (exclusive)
 * @param identity   Identity value for the reduction
 * @param mapFunc    Function: index -> value
 * @param reduceFunc Function: (value, value) -> value
 *
 * Example:
 *   // Sum of squares
 *   int result = parallel_reduce(0, 100, 0,
 *       [](int i) { return i * i; },
 *       [](int a, int b) { return a + b; });
 */
template<typename T, typename MapFunc, typename ReduceFunc>
T parallel_reduce(int begin, int end, T identity, MapFunc&& mapFunc, ReduceFunc&& reduceFunc)
{
	if (begin >= end) {
		return identity;
	}

	ThreadPool& pool = getGlobalThreadPool();
	const int totalItems = end - begin;
	const int threadCount = static_cast<int>(pool.getThreadCount());
	const int numChunks = std::min(threadCount * 4, totalItems);
	const int chunkSize = std::max(1, totalItems / numChunks);

	// Each chunk computes a partial result
	std::vector<T> partialResults(numChunks, identity);
	std::vector<std::pair<int, int>> chunkRanges;
	chunkRanges.reserve(numChunks);

	// Calculate chunk ranges
	for (int chunk = 0; chunk < numChunks; ++chunk) {
		const int chunkBegin = begin + chunk * chunkSize;
		int chunkEnd;
		if (chunk == numChunks - 1) {
			chunkEnd = end; // Last chunk takes remainder
		} else {
			chunkEnd = std::min(chunkBegin + chunkSize, end);
		}
		if (chunkBegin < chunkEnd) {
			chunkRanges.emplace_back(chunkBegin, chunkEnd);
		}
	}

	// Resize partial results to match actual chunk count
	partialResults.resize(chunkRanges.size(), identity);

	// Submit chunk jobs
	for (size_t chunk = 0; chunk < chunkRanges.size(); ++chunk) {
		const int chunkBegin = chunkRanges[chunk].first;
		const int chunkEnd = chunkRanges[chunk].second;

		pool.submit([chunk, chunkBegin, chunkEnd, identity, &mapFunc, &reduceFunc, &partialResults]() {
			T localResult = identity;
			for (int i = chunkBegin; i < chunkEnd; ++i) {
				localResult = reduceFunc(localResult, mapFunc(i));
			}
			partialResults[chunk] = localResult;
		});
	}

	pool.waitForAll();

	// Final reduction of partial results (sequential)
	T result = identity;
	for (const T& partial : partialResults) {
		result = reduceFunc(result, partial);
	}

	return result;
}
