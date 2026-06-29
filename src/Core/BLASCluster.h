#pragma once

#include "Core/BaseMesh.h"
#include "Constants.h"

#include "Instance.hlsli"
#include "Light.hlsli"

class SceneGraph;

struct Light;

// Aggregates the geometry of all meshes belonging to a single owner (RE::TESObjectREFR) into one
// BLAS / one TLAS instance, to avoid many small overlapping AABBs. Meshes are referenced (weak_ptr)
// and owned by the SceneGraph registry. Null-owner meshes get a degenerate single-member cluster.
//
// The owner pointer is used only as a grouping key (never dereferenced). Transforms are captured in
// the traversal (while alive): the per-member local-to-owner is baked into each mesh's geometry descs,
// and the instance (owner-world) transform is cached here via SetInstanceTransform.
class BLASCluster
{
	RE::TESObjectREFR* m_Owner = nullptr; // null for orphan (no-owner) clusters; comparison key only

	eastl::vector<eastl::weak_ptr<BaseMesh>> m_Members;

	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;

	nvrhi::rt::AccelStructHandle m_BLAS;

	eastl::string m_Name;

	float3x4 m_InstanceTransform; // owner-world, cached during traversal; used for the TLAS instance
	mutable float3x4 m_PrevInstanceTransform; // previous-frame instance transform for motion vectors
	mutable bool m_HasPrevInstanceTransform = false;
	mutable float m_InstanceRadius = 0.0f; // world-space bounding sphere radius, accumulated from member bounds
	float3 m_ClusterCenter; // cached translation of m_InstanceTransform, invalid when transform changes

	bool m_MembershipDirty = true; // member added/removed/pruned -> full rebuild
	bool m_Updatable = false;      // any member is updatable (dynamic)

	uint32_t m_NumUpdatesSinceRebuild = 0;
	uint64_t m_LastBuild = Constants::INVALID_FRAME_INDEX;

	uint32_t m_InstanceIndex = 0; // TLAS instance slot, assigned during SceneGraph::Update population

	nvrhi::rt::AccelStructDesc MakeDesc(bool update) const;

public:
	explicit BLASCluster(RE::TESObjectREFR* owner);

	void AddMember(const eastl::shared_ptr<BaseMesh>& mesh);

	void RemoveMember(BaseMesh* mesh);

	const auto& GetMembers() const { return m_Members; }

	void SetInstanceTransform(const float3x4& transform) {
		m_InstanceTransform = transform;
		m_ClusterCenter = float3(transform._14, transform._24, transform._34);
	}

	// Grow the world-space bounding sphere to include the given bound (center + radius in world space).
	void GrowBounds(const RE::NiBound& bound);

	// No live members remain.
	bool Empty() const;

	// Rebuilds or refits the BLAS as needed (once per frame), pulling dirty state from its members.
	void BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph);

	nvrhi::rt::IAccelStruct* GetBLAS() const { return m_BLAS; }

	float3x4 GetInstanceTransform() const { return m_InstanceTransform; }

	void SetInstanceIndex(uint32_t index) { m_InstanceIndex = index; }

	uint32_t GetInstanceIndex() const { return m_InstanceIndex; }

	// Appends one MeshData per visible geometry (in BLAS order) and fills the cluster's InstanceData,
	// including per-instance light-affected data calculated from the scene lights.
	// Returns false if the cluster has no visible geometry (skip as a TLAS instance).
	bool GetData(MeshData* meshData, uint32_t& meshCount, InstanceData& outInstance,
	             const eastl::map<RE::BSLight*, Light>& lights,
	             const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData) const;
};
