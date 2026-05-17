#pragma once

#include "PCH.h"

#include "Types/Iterator.h"

template<typename T>
class VectorStorage
{
    eastl::vector<eastl::unique_ptr<T>> m_Items;

    eastl::vector<eastl::unique_ptr<T>> m_AddQueue;
    eastl::vector<T*> m_RemoveQueue;
    mutable std::mutex m_QueueMutex;

public:
    void Add(eastl::unique_ptr<T>&& ptr)
    {
        std::scoped_lock lock(m_QueueMutex);
        m_AddQueue.emplace_back(eastl::move(ptr));
    }

    void Remove(T* ptr)
    {
        std::scoped_lock lock(m_QueueMutex);
        m_RemoveQueue.emplace_back(ptr);
    }

    auto Size() const 
    {
        return m_Items.size();
    }

    void ApplyChanges()
    {
        eastl::vector<eastl::unique_ptr<T>> addQueue;
        eastl::vector<T*> removeQueue;

        {
            std::scoped_lock lock(m_QueueMutex);
            addQueue = eastl::move(m_AddQueue);
            removeQueue = eastl::move(m_RemoveQueue);
        }

        for (auto& item : addQueue)
            m_Items.emplace_back(eastl::move(item));

        for (auto* ptr : removeQueue)
            EraseItem(ptr);
    }

    // frame-time access, main thread only
    template<typename Fn>
    void Read(Fn&& fn) const 
    {
        for (const auto& item : m_Items)
            if (fn(item) != Iterator::Continue)
                break;
    }

    template<typename Fn>
    void Write(Fn&& fn) 
    {
        for (auto& item : m_Items)
            if (fn(item) != Iterator::Continue)
                break;
    }

private:
    void EraseItem(T* ptr)
    {
        auto it = eastl::find_if(
            m_Items.begin(),
            m_Items.end(),
            [ptr](const auto& p) {
                return p.get() == ptr;
            });

        if (it != m_Items.end()) 
        {
            *it = eastl::move(m_Items.back());
            m_Items.pop_back();
        }
    }
};