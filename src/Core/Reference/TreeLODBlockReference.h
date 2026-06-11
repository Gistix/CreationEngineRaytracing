#pragma once
#if defined(SKYRIM)

#include <PCH.h>

#include "Core/Reference/LODBlockReference.h"

// Since we have no release function we use a timer to track LOD lifetime
struct TreeLODBlockReference : LODBlockReference
{
	RE::BGSDistantTreeBlock* block;
	eastl::vector<RE::BGSDistantTreeBlock::InstanceData*> treeInstanceData;	

	TreeLODBlockReference(RE::BGSDistantTreeBlock* a_block)
		: LODBlockReference(a_block->attached), block(a_block) {
	}

	void AddTreeInstanceData(RE::BGSDistantTreeBlock::InstanceData* a_treeInstanceData);

	virtual void UpdateVisibility() override;
};

#endif