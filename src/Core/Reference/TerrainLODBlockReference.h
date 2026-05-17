#pragma once

#include "Core/Reference/LODBlockReference.h"

struct TerrainLODBlockReference : LODBlockReference
{
	RE::BGSTerrainBlock* block;

	bool intersecting = false;
	bool prevIntersecting = false;

	virtual void UpdateVisibility() override;

	void UpdateIntersection();
};