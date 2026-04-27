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
		FirstPersonHidden = 1 << 1,
		DistanceHidden = 1 << 2,
		HiddenModel = 1 << 3
	};

	// Instance form id
	RE::FormID m_FormID;

	// Node ptr
	RE::NiAVObject* m_Node;

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

	Instance(RE::FormID formID, RE::NiAVObject* node, Model* model) : m_FormID(formID), m_Node(node), model(model) { }
	
	void SetDetached(bool detach);

	void SetHiddenModel(bool hidden);

	bool IsDetached() const;

	// A hidden instance is not updated and does not go in AS, this is set externaly
	bool IsHidden() const;

	// Skip AS but still update since this depends on states updated post instance update
	bool SkipAS() const;

	nvrhi::rt::InstanceDesc GetInstanceDesc() const
	{
		auto instanceDesc = nvrhi::rt::InstanceDesc()
			.setInstanceMask(1)
			.setInstanceID(m_TLASInstanceID)
			.setTransform(m_Transform.f)
			.setBLAS(model->blas);

		// Culling adds additional overhead but some geometry (like vanilla hair) has duplicated double sided faces (as opposed to using the kTwoSided shader flag)
		// Without culling this means we would render 4 faces (original back and front + other side of back and other side of front)
		instanceDesc.setFlags(model->GetMeshFlags().all(Mesh::Flags::DoubleSidedGeom) ? nvrhi::rt::InstanceFlags::None : nvrhi::rt::InstanceFlags::TriangleCullDisable);

		return instanceDesc;
	}

	bool SkipUpdate();

	void Update(uint32_t tlasInstanceID);

	auto GetDirtyFlags() const { return m_DirtyFlags; };

	void ClearDirtyState() { m_DirtyFlags = DirtyFlags::None; };
};