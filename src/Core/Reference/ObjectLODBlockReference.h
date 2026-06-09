#pragma once

#include <PCH.h>

#include "Core/Reference/LODBlockReference.h"
#include "Core/Instance.h"

#include "Types/RE/BGSObjectBlock.h"

// Since we have no release function we use a timer to track LOD lifetime
struct ObjectLODBlockReference : LODBlockReference
{
	RE::BGSObjectBlock* block;

	ObjectLODBlockReference(RE::BGSObjectBlock* a_block)
		: LODBlockReference(a_block->attached), block(a_block) { 
	}

	virtual void UpdateVisibility() override;
};