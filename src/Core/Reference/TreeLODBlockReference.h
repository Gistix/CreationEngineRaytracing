#pragma once

#include "Core/Reference/LODBlockReference.h"

// Since we have no release function we use a timer to track LOD lifetime
struct TreeLODBlockReference : LODBlockReference
{
	RE::BGSDistantTreeBlock* block;
	eastl::vector<RE::BGSDistantTreeBlock::InstanceData*> treeInstanceData;	

	virtual void UpdateVisibility() override;
};