#pragma once

#include "PCH.h"

#include "Types/Iterator.h"
#include "Core/Instance.h"

class InstanceManager
{
public:
    struct RemoveParams
    {
        Instance* instance;
        bool releaseModel;
    };

private:
    eastl::vector<eastl::unique_ptr<Instance>> m_Items;

    eastl::vector<eastl::unique_ptr<Instance>> m_AddQueue;
    eastl::vector<RemoveParams> m_RemoveQueue;
    mutable std::mutex m_QueueMutex;

public:

    void Add(eastl::unique_ptr<Instance>&& ptr)
    {
        std::scoped_lock lock(m_QueueMutex);
        m_AddQueue.emplace_back(eastl::move(ptr));
    }

    void Remove(RemoveParams params)
    {
        std::scoped_lock lock(m_QueueMutex);
        m_RemoveQueue.emplace_back(params);
    }

    auto Size() const 
    {
        return m_Items.size();
    }

    void ApplyChanges()
    {
        eastl::vector<eastl::unique_ptr<Instance>> addQueue;
        eastl::vector<RemoveParams> removeQueue;

        {
            std::scoped_lock lock(m_QueueMutex);
            addQueue = eastl::move(m_AddQueue);
            removeQueue = eastl::move(m_RemoveQueue);
        }

        for (auto& item : addQueue)
            m_Items.emplace_back(eastl::move(item));

        for (auto& params : removeQueue) {
            if (params.releaseModel)
                EraseModel(params.instance);

            EraseInstance(params.instance);
        }
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
    void EraseModel(Instance* instance);

    void EraseInstance(Instance* instance)
    {
        auto it = eastl::find_if(
            m_Items.begin(),
            m_Items.end(),
            [instance](const auto& p) {
                return p.get() == instance;
            });

        if (it != m_Items.end()) 
        {
            *it = eastl::move(m_Items.back());
            m_Items.pop_back();
        }
    }
};