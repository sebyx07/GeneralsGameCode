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

// TheSuperHackers @feature Claude 08/01/2026 Parallelization utility tests (TDD)

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "Parallelize.h"
#include <atomic>
#include <vector>
#include <numeric>
#include <algorithm>

using namespace testing;

class ParallelizeTest : public Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

// =============================================================================
// parallel_for Tests
// =============================================================================

TEST_F(ParallelizeTest, ParallelForEmptyRange)
{
	std::atomic<int> counter{0};

	parallel_for(0, 0, [&counter](int) {
		++counter;
	});

	EXPECT_EQ(counter.load(), 0);
}

TEST_F(ParallelizeTest, ParallelForSingleElement)
{
	std::atomic<int> counter{0};

	parallel_for(0, 1, [&counter](int) {
		++counter;
	});

	EXPECT_EQ(counter.load(), 1);
}

TEST_F(ParallelizeTest, ParallelForVisitsAllIndices)
{
	const int size = 100;
	std::vector<std::atomic<int>> visited(size);
	for (auto& v : visited) {
		v = 0;
	}

	parallel_for(0, size, [&visited](int i) {
		visited[i] = 1;
	});

	for (int i = 0; i < size; ++i) {
		EXPECT_EQ(visited[i].load(), 1) << "Index " << i << " was not visited";
	}
}

TEST_F(ParallelizeTest, ParallelForWithNonZeroStart)
{
	const int start = 10;
	const int end = 20;
	std::vector<std::atomic<int>> visited(end);
	for (auto& v : visited) {
		v = 0;
	}

	parallel_for(start, end, [&visited](int i) {
		visited[i] = 1;
	});

	for (int i = 0; i < start; ++i) {
		EXPECT_EQ(visited[i].load(), 0) << "Index " << i << " should not be visited";
	}
	for (int i = start; i < end; ++i) {
		EXPECT_EQ(visited[i].load(), 1) << "Index " << i << " should be visited";
	}
}

TEST_F(ParallelizeTest, ParallelForComputeSum)
{
	const int size = 1000;
	std::vector<int> data(size);
	std::iota(data.begin(), data.end(), 0); // Fill with 0, 1, 2, ..., 999

	std::atomic<long long> sum{0};

	parallel_for(0, size, [&data, &sum](int i) {
		sum += data[i];
	});

	long long expected = static_cast<long long>(size - 1) * size / 2; // Sum of 0..999
	EXPECT_EQ(sum.load(), expected);
}

TEST_F(ParallelizeTest, ParallelForModifyArray)
{
	const int size = 100;
	std::vector<int> data(size, 0);

	parallel_for(0, size, [&data](int i) {
		data[i] = i * 2;
	});

	for (int i = 0; i < size; ++i) {
		EXPECT_EQ(data[i], i * 2);
	}
}

TEST_F(ParallelizeTest, ParallelForWithChunkSize)
{
	const int size = 100;
	std::atomic<int> counter{0};

	parallel_for(0, size, [&counter](int) {
		++counter;
	}, 10); // Chunk size of 10

	EXPECT_EQ(counter.load(), size);
}

TEST_F(ParallelizeTest, ParallelForLargeDataset)
{
	const int size = 100000;
	std::vector<int> data(size);

	parallel_for(0, size, [&data](int i) {
		data[i] = i;
	});

	for (int i = 0; i < size; ++i) {
		EXPECT_EQ(data[i], i);
	}
}

// =============================================================================
// parallel_for_each Tests
// =============================================================================

TEST_F(ParallelizeTest, ParallelForEachEmptyContainer)
{
	std::vector<int> data;
	std::atomic<int> counter{0};

	parallel_for_each(data.begin(), data.end(), [&counter](int&) {
		++counter;
	});

	EXPECT_EQ(counter.load(), 0);
}

TEST_F(ParallelizeTest, ParallelForEachModifyElements)
{
	std::vector<int> data{1, 2, 3, 4, 5};

	parallel_for_each(data.begin(), data.end(), [](int& x) {
		x *= 2;
	});

	EXPECT_THAT(data, ElementsAre(2, 4, 6, 8, 10));
}

TEST_F(ParallelizeTest, ParallelForEachComputeSum)
{
	std::vector<int> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	std::atomic<int> sum{0};

	parallel_for_each(data.begin(), data.end(), [&sum](const int& x) {
		sum += x;
	});

	EXPECT_EQ(sum.load(), 55);
}

TEST_F(ParallelizeTest, ParallelForEachWithVector)
{
	std::vector<std::string> strings{"a", "b", "c", "d"};

	parallel_for_each(strings.begin(), strings.end(), [](std::string& s) {
		s += s; // Double each string
	});

	EXPECT_THAT(strings, ElementsAre("aa", "bb", "cc", "dd"));
}

// =============================================================================
// parallel_reduce Tests
// =============================================================================

TEST_F(ParallelizeTest, ParallelReduceEmptyRange)
{
	int result = parallel_reduce(0, 0, 0, [](int i) { return i; },
		[](int a, int b) { return a + b; });

	EXPECT_EQ(result, 0);
}

TEST_F(ParallelizeTest, ParallelReduceSum)
{
	const int size = 1000;

	int result = parallel_reduce(0, size, 0,
		[](int i) { return i; },
		[](int a, int b) { return a + b; });

	int expected = (size - 1) * size / 2;
	EXPECT_EQ(result, expected);
}

TEST_F(ParallelizeTest, ParallelReduceMax)
{
	std::vector<int> data{3, 1, 4, 1, 5, 9, 2, 6, 5, 3};

	int result = parallel_reduce(0, static_cast<int>(data.size()), INT_MIN,
		[&data](int i) { return data[i]; },
		[](int a, int b) { return std::max(a, b); });

	EXPECT_EQ(result, 9);
}

TEST_F(ParallelizeTest, ParallelReduceProduct)
{
	const int size = 10;

	long long result = parallel_reduce(1, size + 1, 1LL,
		[](int i) { return static_cast<long long>(i); },
		[](long long a, long long b) { return a * b; });

	// 10! = 3628800
	EXPECT_EQ(result, 3628800LL);
}

// =============================================================================
// Global Thread Pool Access Tests
// =============================================================================

TEST_F(ParallelizeTest, GetGlobalThreadPoolReturnsValidPool)
{
	ThreadPool& pool = getGlobalThreadPool();
	EXPECT_GT(pool.getThreadCount(), 0u);
	EXPECT_TRUE(pool.isRunning());
}

TEST_F(ParallelizeTest, GlobalThreadPoolIsSingleton)
{
	ThreadPool& pool1 = getGlobalThreadPool();
	ThreadPool& pool2 = getGlobalThreadPool();
	EXPECT_EQ(&pool1, &pool2);
}

// =============================================================================
// Determinism Tests (Important for Replay Compatibility)
// =============================================================================

TEST_F(ParallelizeTest, ParallelForProducesDeterministicResultsForIndependentWork)
{
	const int size = 100;
	std::vector<int> data1(size);
	std::vector<int> data2(size);

	// Run twice with same logic
	parallel_for(0, size, [&data1](int i) {
		data1[i] = i * i;
	});

	parallel_for(0, size, [&data2](int i) {
		data2[i] = i * i;
	});

	EXPECT_EQ(data1, data2);
}

// =============================================================================
// Integration with ThreadPool Tests
// =============================================================================

TEST_F(ParallelizeTest, ParallelForWithCustomPool)
{
	ThreadPool customPool(2);
	const int size = 50;
	std::atomic<int> counter{0};

	parallel_for(0, size, [&counter](int) {
		++counter;
	}, 1, &customPool);

	EXPECT_EQ(counter.load(), size);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(ParallelizeTest, ParallelForNegativeRange)
{
	std::atomic<int> counter{0};

	parallel_for(10, 5, [&counter](int) {
		++counter;
	});

	EXPECT_EQ(counter.load(), 0) << "Negative range should execute no iterations";
}

TEST_F(ParallelizeTest, ParallelForVerySmallChunkSize)
{
	const int size = 10;
	std::atomic<int> counter{0};

	parallel_for(0, size, [&counter](int) {
		++counter;
	}, 1);

	EXPECT_EQ(counter.load(), size);
}

TEST_F(ParallelizeTest, ParallelForVeryLargeChunkSize)
{
	const int size = 10;
	std::atomic<int> counter{0};

	parallel_for(0, size, [&counter](int) {
		++counter;
	}, 1000);

	EXPECT_EQ(counter.load(), size);
}
