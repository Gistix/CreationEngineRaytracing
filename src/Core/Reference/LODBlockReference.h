#pragma once

#include "Core/Reference/LODBlockReferenceBase.h"
#include "Core/Instance.h"

// Since we have no release function we use a timer to track LOD lifetime
struct LODBlockReference : LODBlockReferenceBase
{
	std::variant<RE::BGSTerrainBlock*, RE::BGSObjectBlock*, RE::BGSDistantTreeBlock*> block;

	virtual void UpdateVisibility() override;
};