#pragma once

#include "Core/Reference/LODBlockReference.h"
#include "Core/Instance.h"

// Since we have no release function we use a timer to track LOD lifetime
struct ObjectLODBlockReference : LODBlockReference
{
	RE::BGSObjectBlock* block;

	virtual void UpdateVisibility() override;
};