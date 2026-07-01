#pragma once
 
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

#include "nvrhi/nvrhi.h"

class WorkerPool
{
	std::vector<std::thread> m_Threads;
	std::queue<std::function<void(nvrhi::CommandListHandle)>> m_Tasks;
	std::mutex m_Mutex;
	std::condition_variable m_Cv;
	std::atomic<size_t> m_Pending{ 0 };
	bool m_Stop = false;
	std::function<nvrhi::CommandListHandle()> m_ClProvider;
	size_t m_NumThreads;

	void WorkerLoop(size_t threadIdx)
	{
		while (true) {
			std::function<void(nvrhi::CommandListHandle)> task;
			{
				std::unique_lock lock(m_Mutex);
				m_Cv.wait(lock, [this] { return m_Stop || !m_Tasks.empty(); });
				if (m_Stop && m_Tasks.empty())
					return;
				task = std::move(m_Tasks.front());
				m_Tasks.pop();
			}

			task(m_WorkerCommandLists[threadIdx]);

			{
				std::scoped_lock lock(m_Mutex);
				m_Pending--;
			}
			m_Cv.notify_all();
		}
	}

public:
	std::vector<nvrhi::CommandListHandle> m_WorkerCommandLists;

	explicit WorkerPool(size_t numThreads, std::function<nvrhi::CommandListHandle()> clProvider)
		: m_NumThreads(numThreads), m_ClProvider(std::move(clProvider))
	{
		m_WorkerCommandLists.resize(m_NumThreads);

		for (size_t i = 0; i < m_NumThreads; i++) {
			m_Threads.emplace_back([this, i] { WorkerLoop(i); });
		}
	}

	~WorkerPool()
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Stop = true;
		}
		m_Cv.notify_all();
		for (auto& t : m_Threads)
			if (t.joinable())
				t.join();
	}

	void Open()
	{
		for (size_t i = 0; i < m_NumThreads; i++) {
			auto& commandList = m_WorkerCommandLists[i];
			commandList = m_ClProvider();
			commandList->open();
		}
	}

	void Enqueue(std::function<void(nvrhi::CommandListHandle)> task)
	{
		m_Pending++;
		{
			std::scoped_lock lock(m_Mutex);
			m_Tasks.push(std::move(task));
		}
		m_Cv.notify_one();
	}

	void Wait()
	{
		if (m_Pending == 0)
			return;
		std::unique_lock lock(m_Mutex);
		m_Cv.wait(lock, [this] { return m_Pending == 0; });
	}

	size_t Pending() const { return m_Pending; }
};
