#pragma once

#include "core/Model.h"

#include "Light.hlsli"

#include "Util.h"

#include "DirtyFlags.h"

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

	RE::NiTransform m_NiTransform;

	// Used for BLAS instance
	float3x4 m_Transform;

	// Makes sure we only update once per frame
	uint64_t m_LastUpdate = 0;

	DirtyFlags m_DirtyFlags = DirtyFlags::None;

	Instance(RE::FormID formID, RE::NiAVObject* node, Model* model) : formID(formID), node(node), model(model) { }
	
	nvrhi::rt::InstanceDesc GetInstanceDesc() const
	{
		nvrhi::rt::InstanceDesc instanceDesc;
		instanceDesc.bottomLevelAS = model->blas;
		assert(instanceDesc.bottomLevelAS);
		instanceDesc.instanceMask = 1;
		instanceDesc.instanceID = 0;
		memcpy(instanceDesc.transform, m_Transform.m, sizeof(instanceDesc.transform));
		return instanceDesc;
	}

	bool SkipUpdate();

	void Update();

	auto GetDirtyFlags() const { return m_DirtyFlags; };

	void ClearDirtyState() { m_DirtyFlags = DirtyFlags::None; };
};