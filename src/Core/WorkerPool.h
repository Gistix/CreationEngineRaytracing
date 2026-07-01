#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class WorkerPool
{
	std::vector<std::thread> m_Threads;
	std::queue<std::function<void()>> m_Tasks;
	std::mutex m_Mutex;
	std::condition_variable m_Cv;
	std::atomic<size_t> m_Pending{ 0 };
	bool m_Stop = false;

	void WorkerLoop()
	{
		while (true) {
			std::function<void()> task;
			{
				std::unique_lock lock(m_Mutex);
				m_Cv.wait(lock, [this] { return m_Stop || !m_Tasks.empty(); });
				if (m_Stop && m_Tasks.empty())
					return;
				task = std::move(m_Tasks.front());
				m_Tasks.pop();
			}
			task();
			{
				std::scoped_lock lock(m_Mutex);
				m_Pending--;
			}
			m_Cv.notify_all();
		}
	}

public:
	explicit WorkerPool(size_t numThreads)
	{
		for (size_t i = 0; i < numThreads; i++)
			m_Threads.emplace_back(&WorkerPool::WorkerLoop, this);
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

	void Enqueue(std::function<void()> task)
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
