#include "ThreadPoolBench.h"
#include "Core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace Bench
{
	namespace
	{
		using Clock = std::chrono::high_resolution_clock;

		struct Stats
		{
			double meanUs = 0.0;
			double medianUs = 0.0;
			double minUs = 0.0;
			double maxUs = 0.0;
			double stdDevUs = 0.0;

			static Stats Compute(std::vector<double>& a_samples)
			{
				if (a_samples.empty()) return {};
				std::sort(a_samples.begin(), a_samples.end());

				const double sum = std::accumulate(a_samples.begin(), a_samples.end(), 0.0);
				const double mean = sum / static_cast<double>(a_samples.size());
				const double median = a_samples[a_samples.size() / 2];
				const double minVal = a_samples.front();
				const double maxVal = a_samples.back();

				double varSum = 0.0;
				for (double s : a_samples) {
					varSum += (s - mean) * (s - mean);
				}
				const double stdDev = std::sqrt(varSum / static_cast<double>(a_samples.size()));

				return { mean, median, minVal, maxVal, stdDev };
			}
		};

		void PrintStatsRow(const std::string& label, const Stats& s)
		{
			std::cout << "  " << std::left << std::setw(32) << label
			          << " | Mean: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.meanUs << " us"
			          << " | Med: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.medianUs << " us"
			          << " | Min: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.minUs << " us"
			          << " | Max: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.maxUs << " us"
			          << " | SD: " << std::right << std::setw(7) << std::fixed << std::setprecision(2) << s.stdDevUs << " us\n";
		}

		void BenchParallelForGrains(ThreadPool& pool, size_t itemCount, const std::string& scenario)
		{
			std::cout << "\n--- ParallelFor Grain Sweep [" << scenario << ": " << itemCount << " items, "
			          << pool.GetThreadCount() << " threads] ---\n";

			const std::vector<size_t> grainSizes = { 1, 4, 8, 16, 32, 64, 128 };
			std::vector<uint32_t> data(itemCount, 0);

			for (size_t grain : grainSizes) {
				// Warmup
				for (int w = 0; w < 5; ++w) {
					pool.ParallelFor(itemCount, grain, [&](size_t /*taskIdx*/, size_t i) {
						data[i] += static_cast<uint32_t>(i * 3 + 1);
					});
				}

				// Measure
				constexpr int kIterations = 100;
				std::vector<double> samples;
				samples.reserve(kIterations);

				for (int it = 0; it < kIterations; ++it) {
					const auto start = Clock::now();
					pool.ParallelFor(itemCount, grain, [&](size_t /*taskIdx*/, size_t i) {
						data[i] ^= static_cast<uint32_t>(i * 2654435761u);
					});
					const auto elapsed = Clock::now() - start;
					samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
				}

				const auto stats = Stats::Compute(samples);
				PrintStatsRow("Grain = " + std::to_string(grain), stats);
			}
		}

		void BenchWorkStealingTaskChurn(size_t numThreads, size_t totalTasks)
		{
			std::cout << "\n--- Work-Stealing Task Churn [" << totalTasks << " tasks, "
			          << numThreads << " threads] ---\n";

			ThreadPool pool(numThreads);
			std::atomic<uint64_t> counter{ 0 };

			constexpr int kIterations = 10;
			std::vector<double> samples;
			samples.reserve(kIterations);

			for (int it = 0; it < kIterations; ++it) {
				counter.store(0, std::memory_order_relaxed);
				const auto start = Clock::now();

				for (size_t t = 0; t < totalTasks; ++t) {
					pool.Enqueue([&counter, t]() {
						counter.fetch_add(t & 0xF, std::memory_order_relaxed);
					});
				}
				pool.WaitAll();

				const auto elapsed = Clock::now() - start;
				samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
			}

			const auto stats = Stats::Compute(samples);
			PrintStatsRow("Enqueue + WaitAll (" + std::to_string(totalTasks) + ")", stats);
			const double opsPerSec = (static_cast<double>(totalTasks) / (stats.meanUs * 1e-6)) / 1e6;
			std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << opsPerSec << " Mtasks/sec\n";
		}

		void BenchSleepWakeLatency(size_t numThreads)
		{
			std::cout << "\n--- Worker Wake Latency (Spin vs CV Park) [" << numThreads << " threads] ---\n";

			ThreadPool pool(numThreads);

			// Test 1: Short pause (20 microseconds - within spin window)
			{
				constexpr int kBursts = 100;
				std::vector<double> samples;
				samples.reserve(kBursts);

				for (int b = 0; b < kBursts; ++b) {
					// Busy-wait or small pause
					const auto pauseStart = Clock::now();
					while (std::chrono::duration<double, std::micro>(Clock::now() - pauseStart).count() < 20.0) {
						_mm_pause();
					}

					const auto start = Clock::now();
					std::atomic<int> completed{ 0 };
					for (size_t i = 0; i < numThreads; ++i) {
						pool.Enqueue([&completed]() {
							completed.fetch_add(1, std::memory_order_relaxed);
						});
					}
					pool.WaitAll();
					const auto elapsed = Clock::now() - start;
					samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
				}
				const auto stats = Stats::Compute(samples);
				PrintStatsRow("Pause: 20 us (Short Spin)", stats);
			}

			// Test 2: Long pause (2,000 microseconds - threads park on CV)
			{
				constexpr int kBursts = 50;
				std::vector<double> samples;
				samples.reserve(kBursts);

				for (int b = 0; b < kBursts; ++b) {
					std::this_thread::sleep_for(std::chrono::microseconds(2000));

					const auto start = Clock::now();
					std::atomic<int> completed{ 0 };
					for (size_t i = 0; i < numThreads; ++i) {
						pool.Enqueue([&completed]() {
							completed.fetch_add(1, std::memory_order_relaxed);
						});
					}
					pool.WaitAll();
					const auto elapsed = Clock::now() - start;
					samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
				}
				const auto stats = Stats::Compute(samples);
				PrintStatsRow("Pause: 2000 us (Kernel CV Park)", stats);
			}
		}
	}

	void RunThreadPoolBenchmarks()
	{
		std::cout << "=================================================================\n";
		std::cout << "                THREADPOOL MICROBENCHMARKS\n";
		std::cout << "=================================================================\n";

		// 1. Task churn with work-stealing
		BenchWorkStealingTaskChurn(6, 50000);

		// 2. Grain size sweep simulating Phase A (1,500 children)
		{
			ThreadPool pool6(6);
			BenchParallelForGrains(pool6, 1500, "Phase A Forked Subtrees");
		}

		// 3. Grain size sweep simulating Phase B (3,500 mesh updates)
		{
			ThreadPool pool6(6);
			BenchParallelForGrains(pool6, 3500, "Phase B Mesh Updates");
		}

		// 4. Thread scaling sweep
		{
			std::cout << "\n--- Thread Count Scaling (Phase A 1,500 items, Grain 16) ---\n";
			for (size_t threads : { 1, 2, 4, 6, 8, 12 }) {
				ThreadPool pool(threads);
				std::vector<uint32_t> data(1500, 0);

				std::vector<double> samples;
				samples.reserve(50);
				for (int it = 0; it < 50; ++it) {
					const auto start = Clock::now();
					pool.ParallelFor(1500, 16, [&](size_t /*t*/, size_t i) {
						data[i] += static_cast<uint32_t>(i);
					});
					const auto elapsed = Clock::now() - start;
					samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
				}
				const auto stats = Stats::Compute(samples);
				PrintStatsRow(std::to_string(threads) + " Worker Threads", stats);
			}
		}

		// 5. Wake/Park latency
		BenchSleepWakeLatency(6);
	}
}
