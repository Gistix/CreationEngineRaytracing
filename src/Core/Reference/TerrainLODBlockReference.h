#pragma once

#include <PCH.h>

#include "Core/Reference/LODBlockReference.h"

struct TerrainLODBlockReference : LODBlockReference
{
	RE::BGSTerrainBlock* block;

	bool intersecting = false;
	bool prevIntersecting = false;

	TerrainLODBlockReference(RE::BGSTerrainBlock* a_block)
		: LODBlockReference(a_block->attached), block(a_block) {
	}

	virtual void UpdateVisibility() override;

	void UpdateIntersection();
};