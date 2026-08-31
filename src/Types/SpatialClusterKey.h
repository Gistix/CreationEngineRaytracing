#pragma once

#include "Types/RE/RE.h"
#include <eastl/hash_map.h>
#include <eastl/vector.h>
#include <eastl/unique_ptr.h>
#include <shared_mutex>
#include <array>
#include <cmath>

struct SpatialClusterKey
{
	int32_t cellX{ 0 };
	int32_t cellY{ 0 };
	int32_t cellZ{ 0 };

	bool operator==(const SpatialClusterKey& other) const = default;
};

struct SpatialClusterKeyHash
{
	size_t operator()(const SpatialClusterKey& k) const
	{
		size_t h = 2166136261u;
		h = (h ^ static_cast<size_t>(k.cellX)) * 16777619u;
		h = (h ^ static_cast<size_t>(k.cellY)) * 16777619u;
		h = (h ^ static_cast<size_t>(k.cellZ)) * 16777619u;
		return h;
	}
};

// Default spatial cell size in game units (~15 meters).
constexpr float DEFAULT_SPATIAL_CELL_SIZE = 1024.0f;

inline SpatialClusterKey GetSpatialKey(const RE::NiPoint3& center, float cellSize = DEFAULT_SPATIAL_CELL_SIZE)
{
	return SpatialClusterKey{
		static_cast<int32_t>(std::floor(center.x / cellSize)),
		static_cast<int32_t>(std::floor(center.y / cellSize)),
		static_cast<int32_t>(std::floor(center.z / cellSize))
	};
}

template <typename Key, typename Value, typename Hash = eastl::hash<Key>, size_t NumShards = 64>
class ShardedConcurrentMap
{
	using RawPointer = typename Value::element_type*;

	struct Shard
	{
		mutable std::shared_mutex mutex;
		eastl::hash_map<Key, Value, Hash> map;
	};

	std::array<Shard, NumShards> m_Shards;

	size_t GetShardIndex(const Key& key) const
	{
		return Hash{}(key) & (NumShards - 1);
	}

public:
	template <typename Factory>
	RawPointer FindOrEmplace(const Key& key, Factory&& factory)
	{
		auto& shard = m_Shards[GetShardIndex(key)];

		// Fast path: shared read lock
		{
			std::shared_lock lock(shard.mutex);
			auto it = shard.map.find(key);
			if (it != shard.map.end())
				return it->second.get();
		}

		// Slow path: exclusive write lock on this shard only
		std::unique_lock lock(shard.mutex);
		auto [it, inserted] = shard.map.try_emplace(key, nullptr);
		if (inserted) {
			it->second = factory();
		}
		return it->second.get();
	}

	template <typename VectorType>
	void CollectAll(VectorType& out)
	{
		for (auto& shard : m_Shards) {
			std::shared_lock lock(shard.mutex);
			for (auto& [_, val] : shard.map) {
				if (val) {
					out.push_back(val.get());
				}
			}
		}
	}

	template <typename Predicate>
	void Prune(Predicate&& shouldPrune)
	{
		for (auto& shard : m_Shards) {
			std::unique_lock lock(shard.mutex);
			for (auto it = shard.map.begin(); it != shard.map.end(); ) {
				if (shouldPrune(it->second.get())) {
					it = shard.map.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	size_t Size() const
	{
		size_t total = 0;
		for (const auto& shard : m_Shards) {
			std::shared_lock lock(shard.mutex);
			total += shard.map.size();
		}
		return total;
	}

	void Clear()
	{
		for (auto& shard : m_Shards) {
			std::unique_lock lock(shard.mutex);
			shard.map.clear();
		}
	}
};
