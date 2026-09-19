#include "WalkerBench.h"
#include "Core/ThreadPool.h"
#include "ankerl/unordered_dense.h"

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

namespace Bench
{
	namespace
	{
		using Clock = std::chrono::high_resolution_clock;

		struct DummyMesh
		{
			uint64_t id;
			char padding[56]; // Simulate ~64 byte BaseMesh header
		};

		struct DummyTriShape
		{
			uint64_t id;
			char padding[56];
		};

		struct DummyRefr
		{
			uint64_t formID;
		};

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

		void PrintStatsRow(const std::string& label, const Stats& s, size_t lookupsPerFrame)
		{
			const double mops = (static_cast<double>(lookupsPerFrame) / (s.meanUs * 1e-6)) / 1e6;
			std::cout << "  " << std::left << std::setw(38) << label
			          << " | Mean: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.meanUs << " us"
			          << " | Med: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.medianUs << " us"
			          << " | Min: " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << s.minUs << " us"
			          << " | SD: " << std::right << std::setw(7) << std::fixed << std::setprecision(2) << s.stdDevUs << " us"
			          << " | " << std::right << std::setw(6) << std::fixed << std::setprecision(2) << mops << " Mops/s\n";
		}

		// Frame traversal simulation dataset
		struct SimulationData
		{
			std::vector<DummyTriShape*> allRegisteredTriShapes;
			std::vector<DummyMesh*> allRegisteredMeshes;
			std::vector<DummyTriShape*> unregisteredTriShapes;
			DummyRefr dummyRefr{ 0x14 };

			// Sequence of visited leaves per frame across N frames
			std::vector<std::vector<DummyTriShape*>> frames;

			static SimulationData Generate(size_t registeredCount, size_t unregisteredCount, size_t leavesPerFrame, size_t frameCount)
			{
				SimulationData data;
				data.allRegisteredTriShapes.reserve(registeredCount);
				data.allRegisteredMeshes.reserve(registeredCount);

				for (size_t i = 0; i < registeredCount; ++i) {
					data.allRegisteredTriShapes.push_back(new DummyTriShape{ i, {} });
					data.allRegisteredMeshes.push_back(new DummyMesh{ i, {} });
				}

				data.unregisteredTriShapes.reserve(unregisteredCount);
				for (size_t i = 0; i < unregisteredCount; ++i) {
					data.unregisteredTriShapes.push_back(new DummyTriShape{ registeredCount + i, {} });
				}

				// Generate randomized frames
				std::mt19937_64 rng(1337);
				std::uniform_int_distribution<size_t> distReg(0, registeredCount - 1);
				std::uniform_int_distribution<size_t> distUnreg(0, unregisteredCount - 1);
				std::bernoulli_distribution isUnregistered(0.08); // 8% newly created objects per frame

				data.frames.resize(frameCount);
				for (size_t f = 0; f < frameCount; ++f) {
					data.frames[f].reserve(leavesPerFrame);
					for (size_t i = 0; i < leavesPerFrame; ++i) {
						if (isUnregistered(rng)) {
							data.frames[f].push_back(data.unregisteredTriShapes[distUnreg(rng)]);
						} else {
							data.frames[f].push_back(data.allRegisteredTriShapes[distReg(rng)]);
						}
					}
				}

				return data;
			}

			~SimulationData()
			{
				for (auto* p : allRegisteredTriShapes) delete p;
				for (auto* p : allRegisteredMeshes) delete p;
				for (auto* p : unregisteredTriShapes) delete p;
			}
		};

		// 1. Baseline: eastl::unordered_map + per-frame assign({}) vector deallocations
		Stats RunBaselineEastl(ThreadPool& pool, const SimulationData& data)
		{
			eastl::unordered_map<DummyTriShape*, DummyMesh*> meshMap;
			meshMap.reserve(data.allRegisteredTriShapes.size());
			for (size_t i = 0; i < data.allRegisteredTriShapes.size(); ++i) {
				meshMap.emplace(data.allRegisteredTriShapes[i], data.allRegisteredMeshes[i]);
			}

			const size_t numWorkers = pool.GetThreadCount();
			const size_t numSlots = numWorkers + 1;

			eastl::vector<eastl::vector<eastl::pair<DummyMesh*, DummyRefr*>>> perWorkerUpdateList;
			eastl::vector<eastl::vector<eastl::pair<DummyTriShape*, DummyRefr*>>> perWorkerCreateList;
			eastl::vector<eastl::vector<DummyMesh*>> perWorkerCurrentVisible;

			eastl::vector<eastl::pair<DummyMesh*, DummyRefr*>> flatUpdateList;
			eastl::vector<eastl::pair<DummyTriShape*, DummyRefr*>> flatCreateList;
			eastl::vector<DummyMesh*> flatCurrentVisible;

			std::vector<double> samples;
			samples.reserve(data.frames.size());

			auto* refr = const_cast<DummyRefr*>(&data.dummyRefr);

			for (const auto& frameLeaves : data.frames) {
				const auto start = Clock::now();

				// Exact pattern from SceneGraph.cpp (lines 494-500)
				perWorkerUpdateList.assign(numSlots, {});
				perWorkerCreateList.assign(numSlots, {});
				perWorkerCurrentVisible.assign(numSlots, {});

				for (auto& v : perWorkerUpdateList) v.reserve(256);
				for (auto& v : perWorkerCreateList) v.reserve(64);
				for (auto& v : perWorkerCurrentVisible) v.reserve(256);

				const size_t totalLeaves = frameLeaves.size();
				pool.ParallelFor(totalLeaves, 16, [&](size_t taskIdx, size_t i) {
					DummyTriShape* shape = frameLeaves[i];
					const size_t slot = taskIdx + 1;

					auto it = meshMap.find(shape);
					if (it != meshMap.end()) {
						DummyMesh* m = it->second;
						perWorkerUpdateList[slot].push_back({ m, refr });
						perWorkerCurrentVisible[slot].push_back(m);
					} else {
						perWorkerCreateList[slot].push_back({ shape, refr });
					}
				});

				flatUpdateList.clear();
				flatCreateList.clear();
				flatCurrentVisible.clear();

				for (auto& w : perWorkerUpdateList) {
					flatUpdateList.insert(flatUpdateList.end(), w.begin(), w.end());
					w.clear();
				}
				for (auto& w : perWorkerCreateList) {
					flatCreateList.insert(flatCreateList.end(), w.begin(), w.end());
					w.clear();
				}
				for (auto& w : perWorkerCurrentVisible) {
					flatCurrentVisible.insert(flatCurrentVisible.end(), w.begin(), w.end());
					w.clear();
				}

				const auto elapsed = Clock::now() - start;
				samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
			}

			return Stats::Compute(samples);
		}

		// 2. Optimization 1: eastl::unordered_map + Retained Buffers (no per-frame assign reallocations)
		Stats RunRetainedBuffersEastl(ThreadPool& pool, const SimulationData& data)
		{
			eastl::unordered_map<DummyTriShape*, DummyMesh*> meshMap;
			meshMap.reserve(data.allRegisteredTriShapes.size());
			for (size_t i = 0; i < data.allRegisteredTriShapes.size(); ++i) {
				meshMap.emplace(data.allRegisteredTriShapes[i], data.allRegisteredMeshes[i]);
			}

			const size_t numWorkers = pool.GetThreadCount();
			const size_t numSlots = numWorkers + 1;

			eastl::vector<eastl::vector<eastl::pair<DummyMesh*, DummyRefr*>>> perWorkerUpdateList(numSlots);
			eastl::vector<eastl::vector<eastl::pair<DummyTriShape*, DummyRefr*>>> perWorkerCreateList(numSlots);
			eastl::vector<eastl::vector<DummyMesh*>> perWorkerCurrentVisible(numSlots);

			for (auto& v : perWorkerUpdateList) v.reserve(256);
			for (auto& v : perWorkerCreateList) v.reserve(64);
			for (auto& v : perWorkerCurrentVisible) v.reserve(256);

			eastl::vector<eastl::pair<DummyMesh*, DummyRefr*>> flatUpdateList;
			eastl::vector<eastl::pair<DummyTriShape*, DummyRefr*>> flatCreateList;
			eastl::vector<DummyMesh*> flatCurrentVisible;

			std::vector<double> samples;
			samples.reserve(data.frames.size());

			auto* refr = const_cast<DummyRefr*>(&data.dummyRefr);

			for (const auto& frameLeaves : data.frames) {
				const auto start = Clock::now();

				// Retained: inner vectors are already allocated with capacity, no assign/reserve churn
				const size_t totalLeaves = frameLeaves.size();
				pool.ParallelFor(totalLeaves, 16, [&](size_t taskIdx, size_t i) {
					DummyTriShape* shape = frameLeaves[i];
					const size_t slot = taskIdx + 1;

					auto it = meshMap.find(shape);
					if (it != meshMap.end()) {
						DummyMesh* m = it->second;
						perWorkerUpdateList[slot].push_back({ m, refr });
						perWorkerCurrentVisible[slot].push_back(m);
					} else {
						perWorkerCreateList[slot].push_back({ shape, refr });
					}
				});

				flatUpdateList.clear();
				flatCreateList.clear();
				flatCurrentVisible.clear();

				for (auto& w : perWorkerUpdateList) {
					flatUpdateList.insert(flatUpdateList.end(), w.begin(), w.end());
					w.clear(); // keeps allocated capacity
				}
				for (auto& w : perWorkerCreateList) {
					flatCreateList.insert(flatCreateList.end(), w.begin(), w.end());
					w.clear();
				}
				for (auto& w : perWorkerCurrentVisible) {
					flatCurrentVisible.insert(flatCurrentVisible.end(), w.begin(), w.end());
					w.clear();
				}

				const auto elapsed = Clock::now() - start;
				samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
			}

			return Stats::Compute(samples);
		}

		// 3. Optimization 2: ankerl::unordered_dense::map + Retained Buffers
		Stats RunAnkerlDenseMap(ThreadPool& pool, const SimulationData& data)
		{
			ankerl::unordered_dense::map<DummyTriShape*, DummyMesh*> meshMap;
			meshMap.reserve(data.allRegisteredTriShapes.size());
			for (size_t i = 0; i < data.allRegisteredTriShapes.size(); ++i) {
				meshMap.emplace(data.allRegisteredTriShapes[i], data.allRegisteredMeshes[i]);
			}

			const size_t numWorkers = pool.GetThreadCount();
			const size_t numSlots = numWorkers + 1;

			eastl::vector<eastl::vector<eastl::pair<DummyMesh*, DummyRefr*>>> perWorkerUpdateList(numSlots);
			eastl::vector<eastl::vector<eastl::pair<DummyTriShape*, DummyRefr*>>> perWorkerCreateList(numSlots);
			eastl::vector<eastl::vector<DummyMesh*>> perWorkerCurrentVisible(numSlots);

			for (auto& v : perWorkerUpdateList) v.reserve(256);
			for (auto& v : perWorkerCreateList) v.reserve(64);
			for (auto& v : perWorkerCurrentVisible) v.reserve(256);

			eastl::vector<eastl::pair<DummyMesh*, DummyRefr*>> flatUpdateList;
			eastl::vector<eastl::pair<DummyTriShape*, DummyRefr*>> flatCreateList;
			eastl::vector<DummyMesh*> flatCurrentVisible;

			std::vector<double> samples;
			samples.reserve(data.frames.size());

			auto* refr = const_cast<DummyRefr*>(&data.dummyRefr);

			for (const auto& frameLeaves : data.frames) {
				const auto start = Clock::now();

				const size_t totalLeaves = frameLeaves.size();
				pool.ParallelFor(totalLeaves, 16, [&](size_t taskIdx, size_t i) {
					DummyTriShape* shape = frameLeaves[i];
					const size_t slot = taskIdx + 1;

					auto it = meshMap.find(shape);
					if (it != meshMap.end()) {
						DummyMesh* m = it->second;
						perWorkerUpdateList[slot].push_back({ m, refr });
						perWorkerCurrentVisible[slot].push_back(m);
					} else {
						perWorkerCreateList[slot].push_back({ shape, refr });
					}
				});

				flatUpdateList.clear();
				flatCreateList.clear();
				flatCurrentVisible.clear();

				for (auto& w : perWorkerUpdateList) {
					flatUpdateList.insert(flatUpdateList.end(), w.begin(), w.end());
					w.clear();
				}
				for (auto& w : perWorkerCreateList) {
					flatCreateList.insert(flatCreateList.end(), w.begin(), w.end());
					w.clear();
				}
				for (auto& w : perWorkerCurrentVisible) {
					flatCurrentVisible.insert(flatCurrentVisible.end(), w.begin(), w.end());
					w.clear();
				}

				const auto elapsed = Clock::now() - start;
				samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
			}

			return Stats::Compute(samples);
		}
	}

	void RunWalkerBenchmarks()
	{
		std::cout << "\n=================================================================\n";
		std::cout << "        PARALLELTRI-SHAPE WALKER / VISIT-LEAF BENCHMARKS\n";
		std::cout << "=================================================================\n";

		constexpr size_t kRegisteredMeshes = 10000;
		constexpr size_t kUnregisteredMeshes = 1000;
		constexpr size_t kLeavesPerFrame = 2500;
		constexpr size_t kFrameCount = 100;

		std::cout << "Simulation Parameters:\n";
		std::cout << "  - Registered Scene Meshes:  " << kRegisteredMeshes << "\n";
		std::cout << "  - Visited Leaves / Frame:   " << kLeavesPerFrame << "\n";
		std::cout << "  - Worker Threads:           6\n";
		std::cout << "  - Frames Measured:          " << kFrameCount << "\n\n";

		auto data = SimulationData::Generate(kRegisteredMeshes, kUnregisteredMeshes, kLeavesPerFrame, kFrameCount);
		ThreadPool pool(6);

		std::cout << "Running benchmarks...\n\n";

		// 1. Baseline
		const auto statsBaseline = RunBaselineEastl(pool, data);
		PrintStatsRow("1. Baseline (eastl + assign alloc)", statsBaseline, kLeavesPerFrame);

		// 2. Retained Buffers
		const auto statsRetained = RunRetainedBuffersEastl(pool, data);
		PrintStatsRow("2. Retained Buffers (eastl map)", statsRetained, kLeavesPerFrame);

		// 3. Ankerl Dense Map
		const auto statsAnkerl = RunAnkerlDenseMap(pool, data);
		PrintStatsRow("3. Ankerl Dense Map + Retained Buffers", statsAnkerl, kLeavesPerFrame);

		std::cout << "\nComparison vs Baseline:\n";
		const double speedupRetained = statsBaseline.meanUs / statsRetained.meanUs;
		const double speedupAnkerl = statsBaseline.meanUs / statsAnkerl.meanUs;
		const double savedUsRetained = statsBaseline.meanUs - statsRetained.meanUs;
		const double savedUsAnkerl = statsBaseline.meanUs - statsAnkerl.meanUs;

		std::cout << "  - Retained Buffers alone:       " << std::fixed << std::setprecision(2)
		          << speedupRetained << "x (" << savedUsRetained << " us / " << (savedUsRetained / 1000.0) << " ms per frame)\n";
		std::cout << "  - Ankerl Dense Map + Retained:  " << std::fixed << std::setprecision(2)
		          << speedupAnkerl << "x (" << savedUsAnkerl << " us / " << (savedUsAnkerl / 1000.0) << " ms per frame)\n";
	}
}
