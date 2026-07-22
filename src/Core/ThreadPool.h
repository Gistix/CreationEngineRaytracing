#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <stop_token>
#include <thread>
#include <vector>

// Work-stealing thread pool.
//
// Each worker owns a private deque of tasks. A worker pushes/pops its own
// work from the front (LIFO -> good cache locality for recursively spawned
// tasks). When a worker's own deque is empty, it tries to steal a task from
// the back of another worker's deque (FIFO from the thief's perspective ->
// steals the oldest, most "independent" work, minimizing contention with
// the victim who is working from the front).
//
// External Enqueue() calls (from outside any worker thread) are distributed
// round-robin across the worker queues. Enqueue() calls made *from inside*
// a worker (e.g. a task spawning subtasks) go straight into that worker's
// own queue, avoiding any cross-thread contention.
//
// Destructor semantics are unchanged from the original: pending tasks are
// allowed to drain before threads exit (cooperative stop), not dropped.
class ThreadPool
{
public:
	explicit ThreadPool(size_t a_count)
	{
		const auto numWorkers = std::max<size_t>(1, a_count);

		m_Queues.reserve(numWorkers);
		for (size_t i = 0; i < numWorkers; ++i)
			m_Queues.push_back(std::make_unique<WorkStealingQueue>());

		m_Workers.reserve(numWorkers);
		for (size_t i = 0; i < numWorkers; ++i)
			m_Workers.emplace_back([this, i](std::stop_token a_stopToken) { Worker(i, a_stopToken); });
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	~ThreadPool()
	{
		for (auto& w : m_Workers)
			w.request_stop();
		m_WakeCV.notify_all();
	}

	[[nodiscard]] size_t GetThreadCount() const { return m_Workers.size(); }

	template <typename F>
	void Enqueue(F&& a_task)
	{
		std::function<void()> task(std::forward<F>(a_task));

		// IMPORTANT: bump the counters BEFORE the task is pushed, i.e.
		// before it becomes visible/poppable by any worker. Workers can
		// be actively polling (not just CV-parked), so if we pushed
		// first, another thread could pop, run, and fetch_sub() the
		// task before this thread's fetch_add() ever executes -- that
		// corrupts the pending count and can make WaitAll() return
		// while real work is still in flight (use-after-free of
		// whatever the caller's stack frame owns).
		m_Pending.fetch_add(1, std::memory_order_release);
		m_QueuedTasks.fetch_add(1, std::memory_order_release);

		const int self = t_WorkerIndex;
		if (self >= 0)
		{
			// Called from inside a worker: keep it local.
			m_Queues[static_cast<size_t>(self)]->Push(std::move(task));
		}
		else
		{
			// Called from an outside thread: spread the load.
			const size_t idx = m_NextQueue.fetch_add(1, std::memory_order_relaxed) % m_Queues.size();
			m_Queues[idx]->Push(std::move(task));
		}

		m_WakeCV.notify_one();
	}

	void WaitAll()
	{
		std::unique_lock lock(m_DoneMutex);
		m_DoneCV.wait(lock, [this] { return m_Pending.load(std::memory_order_acquire) == 0; });
	}

private:
	// Mutex-protected deque. Owner operates on the front (push/pop),
	// thieves steal from the back. Simpler than a lock-free Chase-Lev
	// deque; fine unless steal contention becomes a measured bottleneck.
	class WorkStealingQueue
	{
	public:
		void Push(std::function<void()> a_task)
		{
			std::scoped_lock lock(m_Mutex);
			m_Deque.push_front(std::move(a_task));
		}

		bool TryPop(std::function<void()>& a_out)
		{
			std::scoped_lock lock(m_Mutex);
			if (m_Deque.empty())
				return false;
			a_out = std::move(m_Deque.front());
			m_Deque.pop_front();
			return true;
		}

		bool TrySteal(std::function<void()>& a_out)
		{
			std::scoped_lock lock(m_Mutex);
			if (m_Deque.empty())
				return false;
			a_out = std::move(m_Deque.back());
			m_Deque.pop_back();
			return true;
		}

	private:
		std::deque<std::function<void()>> m_Deque;
		std::mutex m_Mutex;
	};

	bool TryGetTask(size_t a_myIndex, std::function<void()>& a_out)
	{
		if (m_Queues[a_myIndex]->TryPop(a_out))
			return true;

		const size_t n = m_Queues.size();
		for (size_t offset = 1; offset < n; ++offset)
		{
			const size_t victim = (a_myIndex + offset) % n;
			if (m_Queues[victim]->TrySteal(a_out))
				return true;
		}
		return false;
	}

	void Worker(size_t a_myIndex, std::stop_token a_stopToken)
	{
		t_WorkerIndex = static_cast<int>(a_myIndex);

		while (true)
		{
			std::function<void()> task;

			if (TryGetTask(a_myIndex, task))
			{
				m_QueuedTasks.fetch_sub(1, std::memory_order_acq_rel);
			}
			else
			{
				std::unique_lock lock(m_WakeMutex);
				m_WakeCV.wait(lock, [this, &a_stopToken] {
					return a_stopToken.stop_requested() || m_QueuedTasks.load(std::memory_order_acquire) > 0;
					});
				if (a_stopToken.stop_requested() && m_QueuedTasks.load(std::memory_order_acquire) == 0)
					return;
				continue; // re-check queues (own + steal) now that we might have work
			}

			task();

			if (m_Pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				std::scoped_lock lock(m_DoneMutex); // fixes lost-wakeup vs WaitAll's wait registration
				m_DoneCV.notify_one();
			}
		}
	}

	// NOTE ON DECLARATION ORDER: members are destroyed in reverse
	// declaration order. m_Workers MUST be declared last so it is
	// destroyed first -- its jthread destructors join every worker
	// thread before any queue/mutex/CV they touch gets torn down.
	// (Getting this backwards is a real, easy-to-miss UAF: a worker
	// can still be mid-loop, touching m_Queues, while the pool object
	// is unwinding.)
	std::vector<std::unique_ptr<WorkStealingQueue>> m_Queues;

	std::atomic<size_t> m_NextQueue{ 0 };   // round-robins external Enqueue() calls
	std::atomic<size_t> m_QueuedTasks{ 0 }; // tasks sitting in queues, not yet claimed
	std::atomic<size_t> m_Pending{ 0 };     // tasks queued or in-flight, for WaitAll

	std::mutex m_WakeMutex;
	std::condition_variable m_WakeCV;

	std::mutex m_DoneMutex;
	std::condition_variable m_DoneCV;

	static inline thread_local int t_WorkerIndex = -1;

	std::vector<std::jthread> m_Workers;
};