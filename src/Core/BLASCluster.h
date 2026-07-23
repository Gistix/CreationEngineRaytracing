#pragma once

#include "Core/BaseMesh.h"
#include "Constants.h"

#include "Instance.hlsli"
#include "Light.hlsli"

#include <mutex>

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
	enum class BuildMode
	{
		Skip,
		Rebuild,
		Update
	};

	enum Flags
	{
		None = 0,
		Updatable = 1 << 0,
		Player = 1 << 1,
		TwoSided = 1 << 2
	};

	RE::TESObjectREFR* m_Owner = nullptr; // null for orphan (no-owner) clusters; comparison key only

	eastl::vector<BaseMesh*> m_Members;
	eastl::hash_set<BaseMesh*> m_MemberSet;
	mutable std::mutex m_MemberMutex;

	std::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;

	eastl::vector<MeshData> m_MeshData;
	eastl::vector<uint16_t> m_MeshSlots;
	eastl::vector<uint16_t> m_GeometrySlots;

	nvrhi::rt::AccelStructHandle m_BLAS;

	eastl::string m_Name;

	float3x4 m_Transform = Constants::kIdentityTransform;
	float3x4 m_PrevTransform = Constants::kIdentityTransform;
	bool m_NeedsPrevInit = true;

	// cached translation of m_InstanceTransform, invalid when transform changes
	float3 m_ClusterPosition;

	// world-space bounding sphere radius, accumulated from member bounds
	float m_ClusterRadius = 0.0f; 

	friend class SceneGraph;

	uint32_t m_UpdateCount = 0;
	uint64_t m_LastBuildFrame = Constants::INVALID_FRAME_INDEX;

	// TLAS instance slot, assigned during SceneGraph::Update population
	uint32_t m_InstanceIndex = 0; 

	CESEAdapter::REX::EnumSet<Flags> m_Flags = Flags::None;

	CESEAdapter::REX::EnumSet<DirtyFlags> m_DirtyFlags = DirtyFlags::Visibility;
	mutable std::mutex m_DirtyMutex;

	InstanceLightData m_InstanceLightData;

	bool m_IsValid = false;

	void UpdateTransform();
	BuildMode DetermineBuildMode(SceneGraph* sceneGraph, uint64_t frameIndex);

	nvrhi::rt::AccelStructDesc MakeDesc(BuildMode mode) const;

	void UpdateInstanceLightData(
		const eastl::map<RE::BSLight*, Light>& lights,
		const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData);

	void SetValid(bool valid) { m_IsValid = valid; }
public:
	explicit BLASCluster(RE::TESObjectREFR* owner);

	void AddMember(BaseMesh* mesh);
	void RemoveMember(BaseMesh* mesh);

	const auto& GetMembers() const { return m_Members; }

	void UpdateDirtyFlags(const DirtyFlags& meshDirtyFlags);

	// Grow the world-space bounding sphere to include the given bound (center + radius in world space).
	void GrowBounds(const RE::NiBound& bound);

	// No live members remain.
	bool Empty() const;

	// Has visible meshes — valid only after Update() has been called this frame.
	bool Valid() const;

	// Rebuilds or refits the BLAS as needed (once per frame), pulling dirty state from its members.
	void BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph);

	nvrhi::rt::InstanceDesc MakeInstanceDesc() const;

	void SetInstanceIndex(uint32_t index) { m_InstanceIndex = index; }

	// Updates the cluster and returns its visible MeshData entries.
	const eastl::vector<MeshData>& Update();

	const auto& GetMeshSlots() const { return m_MeshSlots; }
	const auto& GetGeometrySlots() const { return m_GeometrySlots; }

	void WriteInstanceData(uint32_t firstMesh, uint32_t meshCount, InstanceData& instanceData) const;
};
