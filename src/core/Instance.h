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

	// Node ptr
	RE::NiAVObject* node;

	// Model ptr
	Model* model;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	uint64_t m_LastUpdate = 0;

	Instance(RE::FormID formID, RE::NiAVObject* node, Model* model) : formID(formID), node(node), model(model) { }
	
	nvrhi::rt::InstanceDesc GetInstanceDesc() const
	{
		nvrhi::rt::InstanceDesc instanceDesc;
		instanceDesc.bottomLevelAS = model->blas;
		assert(instanceDesc.bottomLevelAS);
		instanceDesc.instanceMask = 1;
		instanceDesc.instanceID = 0;
		memcpy(instanceDesc.transform, transform.m, sizeof(transform.m));
		return instanceDesc;
	}

	bool SkipUpdate();

	void Update();
};