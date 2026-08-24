#pragma once

#if defined(FALLOUT4)

#include "RE/B/BSTriShape.h"
#include "RE/B/BSMultiBoundNode.h"

namespace RE
{
	class BGSTerrainManager;
	class BGSObjectBlock;
	class BGSDistantTreeBlock;
	class BGSTerrainBlock;

	class BGSTerrainNode
	{
	public:
		template <class T>
		struct Layer
		{
			Layer*   parent;  // 00
			uint32_t unk08;   // 08
			uint32_t flags;   // 0C
			uint64_t unk10;   // 10
			uint64_t unk18;   // 18
			uint64_t unk20;   // 20
			T*       block;   // 28
		};

		enum class Flag
		{
			kLandHgtAltered = 0x1,
			kLandClrAltered = 0x2,
			kLandTexAltered = 0x4,
			kLandLoaded = 0x8,
			kShaderLandTextureCount = 0x8,
			kLandGoodNormals = 0x10,
			kBlocksize = 0x10,
			kLandHiresHeightfield = 0x20,
			kLandSize = 0x21,
			kLandDataAltered = 0x27,
			kTilesPerBlock = 0x100,
			kTrisPerBlock = 0x200,
			kTriStripIndexCount = 0x3FD,
			kLandRemapped = 0x400,
			kLandArea = 0x441,
			kHalfLand = 0x800,
			kFullLand = 0x1000,
			kInActiveGrid = 0x4000,
			kInLargeRefGrid = 0x8000,
			kLODLevel4 = 0x80000,
			kLODLevel8 = 0x100000,
			kLODLevel16 = 0x200000,
			kLODLevel32 = 0x400000,
			kLODLevel64 = 0x800000,
			kLODLevel128 = 0x1000000,
			kLODLevel256 = 0x2000000,
			kLODLevel512 = 0x4000000
		};

		[[nodiscard]] std::uint32_t GetLODLevel() const { return (nodeState.underlying() >> 21) & 0x3FC; }

		// members
		BGSTerrainManager*                manager;      // 00
		Layer<BGSTerrainBlock>*           terrain;      // 08
		Layer<BGSObjectBlock>*            objects;      // 10
		Layer<BGSDistantTreeBlock>*       trees;        // 18
		Layer<BGSTerrainBlock>*           mapTerrain;   // 20
		Layer<BGSObjectBlock>*            mapObjects;   // 28
		BGSTerrainNode* (*children)[4];                 // 30
		BGSTerrainNode*                   parent;       // 38
		REX::TEnumSet<Flag, std::uint32_t> nodeState;   // 40
		std::uint32_t                     nodeNumber;   // 44
		std::int16_t                      baseCellX;    // 48
		std::int16_t                      baseCellY;    // 4A
		std::uint32_t                     pad4C;        // 4C
	};
	static_assert(sizeof(BGSTerrainNode) == 0x50);

	class BGSTerrainBlock
	{
	public:
		// members
		BGSTerrainNode*   node;           // 00
		BSTriShape*       land;           // 08
		BSMultiBoundNode* water;          // 10
		BSMultiBoundNode* chunk;          // 18
		bool              loaded;         // 20
		bool              attached;       // 21
		bool              waterAttached;  // 22
		bool              unk23;          // 23
		uint32_t          unk24;          // 24
		uint32_t          unk28;          // 28
		uint32_t          unk2C;          // 2C
		uint64_t          unk30;          // 30
		uint64_t          unk38;          // 38
		uint64_t          unk40;          // 40
		uint64_t          unk48;          // 48
		uint64_t          unk50;          // 50
		uint64_t          unk58;          // 58
		uint64_t          unk60;          // 60
		uint64_t          unk68;          // 68
		uint64_t          unk70;          // 70
		uint64_t          unk78;          // 78
		bool              unk80;          // 80
		bool              unk81;          // 81
		bool              unk82;          // 82
		bool              unk83;          // 83
	};
	static_assert(sizeof(BGSTerrainBlock) == 0x88);
}

#endif