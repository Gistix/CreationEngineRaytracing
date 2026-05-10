#pragma once

#include "PCH.h"

namespace safe
{
	enum class Iterator {
		Continue,
		Stop
	};

	template <typename T, typename Allocator = EASTLAllocatorType>
	class vector {
		eastl::vector<T, Allocator> m_Vector;
		mutable std::shared_mutex m_Mutex;

	public:
		template<typename Fn>
		void read(Fn&& fn) const
		{
			std::shared_lock lock(m_Mutex);
			for (const auto& item : m_Vector)
			{
				if (fn(item) == Iterator::Stop)
					break;
			}
		}

		template<typename Fn>
		void write(Fn&& fn)
		{
			std::unique_lock lock(m_Mutex);
			for (auto& item : m_Vector)
			{
				if (fn(item) == Iterator::Stop)
					break;
			}
		}

		inline auto size() const {
			std::shared_lock lock(m_Mutex);
			return m_Vector.size();
		}

		inline void clear() {
			std::unique_lock lock(m_Mutex);
			m_Vector.clear();
		}

		inline void push_back(T& item) {
			std::unique_lock lock(m_Mutex);
			m_Vector.push_back(item);
		}

		inline void push_back(T&& item) {
			std::unique_lock lock(m_Mutex);
			m_Vector.push_back(eastl::move(item));
		}

		template<class... Args>
		inline auto& emplace_back(Args&&... args) {
			std::unique_lock lock(m_Mutex);
			return m_Vector.emplace_back(eastl::forward<Args>(args)...);
		}

		inline void erase(auto item) {
			auto it = eastl::find_if(
				m_Vector.begin(),
				m_Vector.end(),
				[&](const auto& p)
				{
					return p.get() == item;
				});

			if (it != m_Vector.end())
			{
				*it = eastl::move(m_Vector.back());
				m_Vector.pop_back();
			}
		}
	};
}