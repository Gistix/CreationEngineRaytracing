#pragma once
 
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

// Forward declare nvrhi types to avoid heavy includes in the header
namespace nvrhi { class ICommandList; }
using CommandListHandle = nvrhi::ICommandList*;

class WorkerPool
{
	std::vector<std::thread> m_Threads;
	std::queue<std::function<void(CommandListHandle)>> m_Tasks;
	std::mutex m_Mutex;
	std::condition_variable m_Cv;
	std::atomic<size_t> m_Pending{ 0 };
	bool m_Stop = false;
	std::function<CommandListHandle()> m_ClProvider;

	void WorkerLoop(size_t threadIdx)
	{
		while (true) {
			std::function<void(CommandListHandle)> task;
			{
				std::unique_lock lock(m_Mutex);
				m_Cv.wait(lock, [this] { return m_Stop || !m_Tasks.empty(); });
				if (m_Stop && m_Tasks.empty())
					return;
				task = std::move(m_Tasks.front());
				m_Tasks.pop();
			}

			CommandListHandle cl = m_WorkerCommandLists[threadIdx];
			if (cl) {
				task(cl);
			}

			{
				std::scoped_lock lock(m_Mutex);
				m_Pending--;
			}
			m_Cv.notify_all();
		}
	}

public:
	std::vector<CommandListHandle> m_WorkerCommandLists;

	explicit WorkerPool(size_t numThreads, std::function<CommandListHandle()> clProvider)
		: m_ClProvider(std::move(clProvider))
	{
		m_WorkerCommandLists.resize(numThreads);
		for (size_t i = 0; i < numThreads; i++)
			m_Threads.emplace_back([this, i] { WorkerLoop(i); });
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

	void Enqueue(std::function<void(CommandListHandle)> task)
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
