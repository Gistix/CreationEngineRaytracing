#pragma once

#include "core/Model.h"

#include "Light.hlsli"

#include "Util.h"

#include "DirtyFlags.h"

struct Instance
{
	enum State : uint8_t
	{
		None = 0,
		Detached = 1 << 0,
		FirstPersonHidden = 1 << 1
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
	float3x4 m_PrevTransform;

	// Makes sure we only update once per frame
	uint64_t m_LastUpdate = 0;

	uint32_t m_TLASInstanceID = 0;

	stl::enumeration<State> m_State = State::None;

	DirtyFlags m_DirtyFlags = DirtyFlags::None;

	Instance(RE::FormID formID, RE::NiAVObject* node, Model* model) : formID(formID), node(node), model(model) { }
	
	void SetDetached(bool detach);

	bool IsDetached() const;

	bool IsHidden() const;

	nvrhi::rt::InstanceDesc GetInstanceDesc() const
	{
		nvrhi::rt::InstanceDesc instanceDesc;
		instanceDesc.bottomLevelAS = model->blas;
		assert(instanceDesc.bottomLevelAS);
		instanceDesc.instanceMask = 1;
		instanceDesc.instanceID = m_TLASInstanceID;
		memcpy(instanceDesc.transform, m_Transform.f, sizeof(float[12]));
		return instanceDesc;
	}

	bool SkipUpdate();

	void Update(uint32_t tlasInstanceID);

	auto GetDirtyFlags() const { return m_DirtyFlags; };

	void ClearDirtyState() { m_DirtyFlags = DirtyFlags::None; };
};