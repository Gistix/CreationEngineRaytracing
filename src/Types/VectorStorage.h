#pragma one

#include "PCH.h"

#include "Types/safe.h"

template<typename T>
class VectorStorage
{
    safe::vector<eastl::unique_ptr<T>> m_Items;

    safe::vector<eastl::unique_ptr<T>> m_AddQueue;
    safe::vector<T*> m_RemoveQueue;

public:
    template<typename Fn>
    void Read(Fn&& fn) const {
        m_Items.read(fn);
    }

    template<typename Fn>
    void Write(Fn&& fn) {
        m_Items.write(fn);
    }

    auto Size() const
    {
        return m_Items.size();
    }

    void Add(eastl::unique_ptr<T>&& ptr)
    {
        m_AddQueue.emplace_back(eastl::move(ptr));
    }

    void Remove(auto* ptr)
    {
        m_RemoveQueue.emplace_back(ptr);
    }

    void ApplyChanges()
    {
        // Add
        m_AddQueue.write([&](auto& item) {
            m_Items.push_back(eastl::move(item));
            return safe::Iterator::Continue;
        });

        m_AddQueue.clear();

        // Remove
        m_RemoveQueue.write([&](auto& ptr) {
            m_Items.erase(ptr);
            return safe::Iterator::Continue;
        });

        m_RemoveQueue.clear();
    }
};