#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

// NOTE: a single shared m_Mutex/m_Tasks queue is used for simplicity.
// This serializes all Enqueue/dequeue traffic across every worker, which
// is fine for moderate submission rates. If you're enqueueing at very
// high frequency from many producer threads, consider per-worker queues
// with work-stealing instead.
class ThreadPool
{
public:
	// Destructor semantics: NOT immediate cancellation. All tasks already
	// in the queue at destruction time are allowed to finish running;
	// only after the queue drains do worker threads exit. If you need
	// "stop right now, drop pending work," add an explicit Cancel() that
	// clears m_Tasks under the lock before requesting stop.
	explicit ThreadPool(size_t a_count)
	{
		assert(a_count > 0 && "ThreadPool created with zero threads will never run enqueued tasks");
		m_Workers.reserve(a_count);
		for (size_t i = 0; i < a_count; ++i)
			m_Workers.emplace_back([this](std::stop_token a_stopToken) { Worker(a_stopToken); });
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	~ThreadPool()
	{
		for (auto& w : m_Workers)
			w.request_stop();
		m_WorkCV.notify_all();
	}

	[[nodiscard]] size_t GetThreadCount() const { return m_Workers.size(); }

	template <typename F>
	void Enqueue(F&& a_task)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Tasks.push(std::forward<F>(a_task));
		}
		m_Pending.fetch_add(1, std::memory_order_release);
		m_WorkCV.notify_one();
	}

	void WaitAll()
	{
		std::unique_lock lock(m_Mutex);
		m_DoneCV.wait(lock, [this] { return m_Pending.load(std::memory_order_acquire) == 0; });
	}

private:
	void Worker(std::stop_token a_stopToken)
	{
		while (true)
		{
			std::function<void()> task;
			{
				std::unique_lock lock(m_Mutex);
				m_WorkCV.wait(lock, [this, &a_stopToken] { return a_stopToken.stop_requested() || !m_Tasks.empty(); });
				if (a_stopToken.stop_requested() && m_Tasks.empty())
					return;
				task = std::move(m_Tasks.front());
				m_Tasks.pop();
			}
			task();
			if (m_Pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				std::scoped_lock lock(m_Mutex); // synchronizes with WaitAll's wait registration; fixes lost-wakeup
				m_DoneCV.notify_one();
			}
		}
	}

	std::vector<std::jthread> m_Workers;
	std::queue<std::function<void()>> m_Tasks;
	std::mutex m_Mutex;
	std::condition_variable m_WorkCV;
	std::condition_variable m_DoneCV;
	std::atomic<size_t> m_Pending{ 0 };
};