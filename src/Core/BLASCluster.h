#pragma once

#include "Core/BaseMesh.h"
#include "Constants.h"

class SceneGraph;

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

	bool m_MembershipDirty = true; // member added/removed/pruned -> full rebuild
	bool m_Updatable = false;      // any member is updatable (dynamic)

	uint32_t m_NumUpdatesSinceRebuild = 0;
	uint64_t m_LastBuild = Constants::INVALID_FRAME_INDEX;

	nvrhi::rt::AccelStructDesc MakeDesc(bool update) const;

public:
	explicit BLASCluster(RE::TESObjectREFR* owner);

	void AddMember(const eastl::shared_ptr<BaseMesh>& mesh);

	void RemoveMember(BaseMesh* mesh);

	void SetInstanceTransform(const float3x4& transform) { m_InstanceTransform = transform; }

	// No live members remain.
	bool Empty() const;

	// Rebuilds or refits the BLAS as needed (once per frame), pulling dirty state from its members.
	void BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph);

	nvrhi::rt::IAccelStruct* GetBLAS() const { return m_BLAS; }

	float3x4 GetInstanceTransform() const { return m_InstanceTransform; }
};
