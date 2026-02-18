#pragma once

#include "core/Model.h"

struct Instance
{
	enum State : uint8_t
	{
		Hidden = 1 << 0,
		Detached = 1 << 1
	};

	// Instance form id
	RE::FormID formID;

	// Model ptr
	Model* model;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	uint64_t lastUpdate = 0;
};