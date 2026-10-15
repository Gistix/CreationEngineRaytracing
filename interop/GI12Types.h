/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#pragma once

#include <cstdint>

namespace GI12
{
	enum ScreenProbesDebugModes : uint32_t
	{
		SCREENPROBES_DEBUG_RADIANCE = 0,
		SCREENPROBES_DEBUG_RADIANCE_PER_DIRECTION = 1
	};

	enum HashGridCacheDebugMode : uint32_t
	{
		HASHGRIDCACHE_DEBUG_RADIANCE = 0,
		HASHGRIDCACHE_DEBUG_RADIANCE_SAMPLE_COUNT = 1,
		HASHGRIDCACHE_DEBUG_FILTERED_RADIANCE = 2,
		HASHGRIDCACHE_DEBUG_FILTERING_GAIN = 3,
		HASHGRIDCACHE_DEBUG_FILTERED_SAMPLE_COUNT = 4,
		HASHGRIDCACHE_DEBUG_FILTERED_MIP_LEVEL = 5,
		HASHGRIDCACHE_DEBUG_TILE_OCCUPANCY = 6
	};

	namespace Constants
	{
		inline constexpr uint32_t kProbeSize = 8;
		inline constexpr uint32_t kProbeSpawnTileSize = 16;
		inline constexpr uint32_t kMaxRayCount = 64;
		inline constexpr uint32_t kNumCells = 0x40000U;
		inline constexpr uint32_t kNumEntriesPerCell = 0x10U;
		inline constexpr uint32_t kNumEntries = kNumCells * kNumEntriesPerCell;
	}
}
