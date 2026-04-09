#pragma once

#include <eastl/unordered_set.h>

class LockedGeometry
{
public:
    LockedGeometry(RE::BSGeometry* ptr, std::shared_lock<std::shared_mutex> lk)
        : _ptr(ptr), _lock(std::move(lk)) {
    }

    RE::BSGeometry* operator->() const noexcept { return _ptr; }
    RE::BSGeometry* get()        const noexcept { return _ptr; }
    explicit operator bool()     const noexcept { return _ptr != nullptr; }

private:
    RE::BSGeometry* _ptr;
    std::shared_lock<std::shared_mutex> _lock;
};

class WeakBSGeometry
{
public:
    explicit WeakBSGeometry(RE::BSGeometry* ptr = nullptr) : _ptr(ptr)
    {
        if (ptr) {
            std::unique_lock lock(mutex);
            alive.emplace(ptr);
        }
    }

    // Disable copy and move - ownership semantics are unclear otherwise
    WeakBSGeometry(const WeakBSGeometry&) = delete;
    WeakBSGeometry& operator=(const WeakBSGeometry&) = delete;
    WeakBSGeometry(WeakBSGeometry&&) = delete;
    WeakBSGeometry& operator=(WeakBSGeometry&&) = delete;

    static void Erase(RE::BSGeometry* ptr)
    {
        std::unique_lock lock(mutex);
        alive.erase(ptr);
    }

    // The only safe way to use the pointer - lock is held for the lifetime of the result
    [[nodiscard]] LockedGeometry lock_raii() const
    {
        std::shared_lock lock(mutex);
        return { alive.contains(_ptr) ? _ptr : nullptr, std::move(lock) };
    }

private:
    RE::BSGeometry* _ptr{};

    static eastl::unordered_set<RE::BSGeometry*> alive;
    static std::shared_mutex                     mutex;
};