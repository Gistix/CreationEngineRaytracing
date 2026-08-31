#pragma once

#include "Types/AABB.h"
#include <eastl/vector.h>
#include <eastl/hash_map.h>
#include <cmath>
#include <cstdint>

namespace Clustering
{
	// Disjoint Set Union (DSU) for fast component merging
	struct DSU
	{
		eastl::vector<int32_t> parent;
		eastl::vector<int32_t> rank;

		explicit DSU(size_t n = 0)
		{
			Reset(n);
		}

		void Reset(size_t n)
		{
			parent.resize(n);
			rank.assign(n, 0);
			for (size_t i = 0; i < n; ++i)
				parent[i] = static_cast<int32_t>(i);
		}

		int32_t Find(int32_t x)
		{
			if (parent[x] != x)
				parent[x] = Find(parent[x]);
			return parent[x];
		}

		bool Union(int32_t x, int32_t y)
		{
			int32_t rootX = Find(x);
			int32_t rootY = Find(y);
			if (rootX == rootY)
				return false;

			if (rank[rootX] < rank[rootY]) {
				parent[rootX] = rootY;
			} else if (rank[rootX] > rank[rootY]) {
				parent[rootY] = rootX;
			} else {
				parent[rootY] = rootX;
				rank[rootX]++;
			}
			return true;
		}
	};

	// Threshold constants matching NVIDIA RTX Best Practices
	constexpr float SPARSITY_FILL_RATIO_THRESHOLD = 0.25f; // Split if sub-mesh volume / bounding volume < 25% (Figure 1)
	constexpr float OVERLAP_RATIO_THRESHOLD = 0.40f;      // Merge if overlap volume / min volume >= 40% (Figure 2)
	constexpr float BROADPHASE_CELL_SIZE = 256.0f;        // 256 game units (~4 meters) for broadphase neighbor queries

	// 1. Figure 1: Compute Fill Ratio (Individual Volumes / Enclosing AABB Volume)
	inline float ComputeFillRatio(const eastl::vector<AABB>& aabbs, const AABB& enclosingAABB)
	{
		const float clusterVolume = enclosingAABB.GetVolume();
		if (clusterVolume <= 0.0f)
			return 1.0f;

		float sumVolume = 0.0f;
		for (const auto& box : aabbs) {
			sumVolume += box.GetVolume();
		}

		return sumVolume / clusterVolume;
	}

	// Broadphase cell key for spatial neighbor queries
	struct BroadphaseCellKey
	{
		int32_t x{ 0 };
		int32_t y{ 0 };
		int32_t z{ 0 };

		bool operator==(const BroadphaseCellKey& other) const = default;
	};

	struct BroadphaseCellKeyHash
	{
		size_t operator()(const BroadphaseCellKey& k) const
		{
			size_t h = 2166136261u;
			h = (h ^ static_cast<size_t>(k.x)) * 16777619u;
			h = (h ^ static_cast<size_t>(k.y)) * 16777619u;
			h = (h ^ static_cast<size_t>(k.z)) * 16777619u;
			return h;
		}
	};

	// Spatial hash broadphase for accelerated neighbor pair candidate generation
	struct SpatialAABBBroadphase
	{
		float cellSize{ BROADPHASE_CELL_SIZE };
		eastl::hash_map<BroadphaseCellKey, eastl::vector<uint32_t>, BroadphaseCellKeyHash> grid;

		void Clear()
		{
			grid.clear();
		}

		void Insert(uint32_t itemIndex, const AABB& box)
		{
			if (!box.IsValid())
				return;

			const int32_t minX = static_cast<int32_t>(std::floor(box.Min.x / cellSize));
			const int32_t maxX = static_cast<int32_t>(std::floor(box.Max.x / cellSize));
			const int32_t minY = static_cast<int32_t>(std::floor(box.Min.y / cellSize));
			const int32_t maxY = static_cast<int32_t>(std::floor(box.Max.y / cellSize));
			const int32_t minZ = static_cast<int32_t>(std::floor(box.Min.z / cellSize));
			const int32_t maxZ = static_cast<int32_t>(std::floor(box.Max.z / cellSize));

			// Guard against huge bounding boxes spanning thousands of cells
			if ((maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1) > 64) {
				// Insert by center only for exceptionally large boxes
				const float3 c = box.GetCenter();
				BroadphaseCellKey k{
					static_cast<int32_t>(std::floor(c.x / cellSize)),
					static_cast<int32_t>(std::floor(c.y / cellSize)),
					static_cast<int32_t>(std::floor(c.z / cellSize))
				};
				grid[k].push_back(itemIndex);
				return;
			}

			for (int32_t x = minX; x <= maxX; ++x) {
				for (int32_t y = minY; y <= maxY; ++y) {
					for (int32_t z = minZ; z <= maxZ; ++z) {
						grid[BroadphaseCellKey{ x, y, z }].push_back(itemIndex);
					}
				}
			}
		}
	};
}
