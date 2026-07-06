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

	eastl::vector<BaseMesh*> m_Members;
	eastl::hash_set<BaseMesh*> m_MemberSet;

	std::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;

	nvrhi::rt::AccelStructHandle m_BLAS;

	eastl::string m_Name;

	float3x4 m_Transform;
	float3x4 m_PrevTransform;

	// cached translation of m_InstanceTransform, invalid when transform changes
	float3 m_ClusterPosition;

	// world-space bounding sphere radius, accumulated from member bounds
	float m_ClusterRadius = 0.0f; 

	// any member is updatable (dynamic)
	bool m_Updatable = false;

	bool m_IsPlayer = false;

	friend class SceneGraph;

	uint32_t m_NumUpdatesSinceRebuild = 0;
	uint64_t m_LastBuild = Constants::INVALID_FRAME_INDEX;

	// TLAS instance slot, assigned during SceneGraph::Update population
	uint32_t m_InstanceIndex = 0; 

	CESEAdapter::REX::EnumSet<DirtyFlags> m_DirtyFlags = DirtyFlags::Visibility;

	void UpdateTransform();

	nvrhi::rt::AccelStructDesc MakeDesc(bool update) const;

	InstanceLightData GetInstanceLightData(
		const eastl::map<RE::BSLight*, Light>& lights,
		const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData);
public:
	explicit BLASCluster(RE::TESObjectREFR* owner);

	void AddMember(BaseMesh* mesh);
	void RemoveMember(BaseMesh* mesh);

	const auto& GetMembers() const { return m_Members; }

	// Grow the world-space bounding sphere to include the given bound (center + radius in world space).
	void GrowBounds(const RE::NiBound& bound);

	// No live members remain.
	bool Empty() const;

	// Has visible meshes
	bool Valid() const;
	
	// Rebuilds or refits the BLAS as needed (once per frame), pulling dirty state from its members.
	void BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph);

	nvrhi::rt::InstanceDesc MakeInstanceDesc() const;

	void SetInstanceIndex(uint32_t index) { m_InstanceIndex = index; }

	uint32_t GetInstanceIndex() const { return m_InstanceIndex; }

	// Appends one MeshData per visible geometry (in BLAS order) and fills the cluster's InstanceData,
	// including per-instance light-affected data calculated from the scene lights.
	// Returns false if the cluster has no visible geometry (skip as a TLAS instance).
	void Update(MeshData* meshData, uint32_t& meshCount, 
		InstanceData* instanceData, uint32_t& instanceCount,
	    const eastl::map<RE::BSLight*, Light>& lights,
	    const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData);
};
