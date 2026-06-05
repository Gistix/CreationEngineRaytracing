#pragma once

#include "PCH.h"

#include "RE/B/BSTHashMap.h"

namespace RE
{
    struct BSTHashTableParent
    {
    protected:
        using size_type = std::uint32_t;
        std::uint32_t _pad00{ 0 };    // +0x00
        size_type     _capacity{ 0 }; // +0x04  (bucketCount)
        size_type     _free{ 0 };     // +0x08  (freeCount)
        size_type     _good{ 0 };     // +0x0C
    };
    static_assert(sizeof(BSTHashTableParent) == 0x10);

    template <class Key, class T, class Hash = BSCRC32<Key>, class KeyEq = std::equal_to<Key>>
    using BSTCustomHashMap =
        BSTScatterTable<
        Hash,
        KeyEq,
        BSTScatterTableTraits<Key, T>,
        BSTScatterTableHeapAllocator,
        BSTHashTableParent>;
}