#pragma once

#include "Core/BaseMesh.h"
#include "Core/BLASCluster.h"
#include "Core/ThreadPool.h"

#include "Core/TransformManager.h"
#include "core/Light.h"
#include "core/MaterialManager.h"
#include "Core/TextureManager.h"

#include "Light.hlsli"
#include "Mesh.hlsli"
#include "Instance.hlsli"
#include "Transform.hlsli"
#include "Interop/LandLODUpdate.hlsli"

#include "Constants.h"
#include "Types/BindlessTableManager.h"
#include "Types/BindlessTable.h"
#include "Types/VectorStorage.h"
#include "Types/RE/RE.h"
#include "Types/RingBuffer.h"

#include <eastl/array.h>
#include <eastl/vector_set.h>
#include <eastl/unordered_set.h>

#include "Types/PassTiming.h"

#include <shared_mutex>

class LandLODMesh;
class SubIndexSegmentMesh;

class SceneGraph
{
	RE::NiCamera* m_Camera = nullptr;

	eastl::unordered_map<RE::BSTriShape*, eastl::unique_ptr<BaseMesh>> m_DirectMeshes;
	eastl::vector<BaseMesh*> m_CurrentVisible;
	eastl::vector<BaseMesh*> m_PreviousVisible;

	// One BLAS instance per owner reference
	eastl::unordered_map<RE::TESObjectREFR*, eastl::unique_ptr<BLASCluster>> m_OwnerClusters;

	// Meshes without an owner get a degenerate per-mesh cluster
	eastl::unordered_map<RE::BSTriShape*, eastl::unique_ptr<BLASCluster>> m_OrphanClusters;

	// One BLAS instance per SubIndexMesh segment
	// Each SubIndexSegmentMesh lives in its own cluster so it gets its own BLAS
	eastl::unordered_map<SubIndexSegmentMesh*, eastl::unique_ptr<BLASCluster>> m_SubIndexSegmentClusters;

	eastl::vector<BLASCluster*> m_AllClusters;

	std::mutex m_BLASClusterUpdateMutex;

	eastl::vector<RE::BSTriShape*> m_DestroyedMeshes;
	mutable std::mutex m_MeshDestroyMutex;

	// Material manager
	eastl::shared_ptr<MaterialManager> m_MaterialManager;

	// LOD
	eastl::unordered_map<LandLODMesh*, LandLODUpdate> m_LandLODMeshUpdates;

	eastl::unordered_set<RE::BSLight*> m_TempActiveLights;
	eastl::map<RE::BSLight*, Light> m_Lights;

	eastl::array<LightData, Constants::LIGHTS_MAX> m_LightData;
	RingBuffer m_LightBuffer;

	// Mesh
	eastl::array<MeshData, Constants::NUM_MESHES_MAX> m_MeshData;
	RingBuffer m_MeshBuffer;

	// Instance
	eastl::array<InstanceData, Constants::NUM_INSTANCES_MAX> m_InstanceData;
	RingBuffer m_InstanceBuffer;

	eastl::unique_ptr<TextureManager> m_TextureManager;

	eastl::unique_ptr<BindlessTableManager> m_TriangleDescriptors;
	eastl::unique_ptr<BindlessTableManager> m_VertexDescriptors;

	eastl::unique_ptr<BindlessTableManager> m_DynamicVertexReadDescriptors;
	eastl::unique_ptr<BindlessTable> m_SkinningDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexCopyDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexWriteDescriptors;
	eastl::unique_ptr<BindlessTable> m_DynamicVertexDescriptors;
	eastl::unique_ptr<BindlessTable> m_DynamicVertexLiveDescriptors;
	eastl::unique_ptr<BindlessTable> m_PrevPositionDescriptors;
	eastl::unique_ptr<BindlessTable> m_PrevPositionWriteDescriptors;

	uint32_t m_NumMeshes = 0;
	uint32_t m_NumInstances = 0;

	uint64_t m_LastMaintenanceFrame = Constants::INVALID_FRAME_INDEX;
	uint32_t m_MaintenanceRebuildsThisFrame = 0;
	eastl::hash_set<BLASCluster*> m_DirtyClusters;

	// Transform buffer managed by TransformManager
	eastl::unique_ptr<TransformManager> m_TransformManager;

	std::shared_mutex m_OwnerClusterMutex;
	std::shared_mutex m_OrphanClusterMutex;
	std::shared_mutex m_SegmentClusterMutex;

	mutable std::mutex m_ClusterDirtyMutex;

	eastl::unique_ptr<ThreadPool> m_ThreadPool;
	eastl::vector<eastl::pair<BaseMesh*, RE::TESObjectREFR*>> m_UpdateList;
	eastl::vector<eastl::pair<RE::BSTriShape*, RE::TESObjectREFR*>> m_CreateList;

	struct MeshCreateCandidate {
		RE::BSTriShape* bsTriShape;
		RE::TESObjectREFR* refr;
	};
	eastl::vector<MeshCreateCandidate> m_CreateCandidates;
	eastl::vector<eastl::vector<MeshCreateCandidate>> m_PerWorkerCreateCandidates;
	
	// Mesh helpers: route meshes into per-owner BLAS clusters (owner pointer used as key only).

	BLASCluster* GetOrCreateCluster(RE::TESObjectREFR* owner, RE::BSTriShape* bsTriShape);
	template <typename Key, typename Map>
	BLASCluster* GetOrCreateClusterImpl(Map& a_map, std::shared_mutex& a_mutex, Key a_key, RE::TESObjectREFR* a_owner);

	struct PerThreadResult
	{
		eastl::vector<eastl::pair<LightData, RE::BSLight*>> lights;
		eastl::vector<LightData> orphanLights;
		eastl::vector<eastl::pair<MeshData, RE::BSTriShape*>> meshes;
		eastl::vector<eastl::pair<InstanceData, RE::TESObjectREFR*>> instances;
	};
public:
	void Initialize();

	inline auto& GetTriangleDescriptors() const { return m_TriangleDescriptors; }
	inline auto& GetVertexDescriptors() const { return m_VertexDescriptors; }
	inline auto& GetTextureDescriptors() const { return m_TextureManager->m_TextureDescriptors; }
	inline auto& GetCubemapDescriptors() const { return m_TextureManager->m_CubemapDescriptors; }
	inline auto& GetDynamicVertexReadDescriptors() const { return m_DynamicVertexReadDescriptors; }
	inline auto& GetSkinningDescriptors() const { return m_SkinningDescriptors; }
	inline auto& GetVertexCopyDescriptors() const { return m_VertexCopyDescriptors; }
	inline auto& GetVertexWriteDescriptors() const { return m_VertexWriteDescriptors; }
	inline auto& GetDynamicVertexWriteDescriptors() const { return m_DynamicVertexDescriptors; }
	inline auto& GetDynamicVertexDescriptors() const { return m_DynamicVertexLiveDescriptors; }
	inline auto& GetPrevPositionDescriptors() const { return m_PrevPositionDescriptors; }
	inline auto& GetPrevPositionWriteDescriptors() const { return m_PrevPositionWriteDescriptors; }

	nvrhi::IBuffer* GetLightBuffer() const;
	nvrhi::IBuffer* GetMeshBuffer() const;
	nvrhi::IBuffer* GetInstanceBuffer() const;
	nvrhi::IBuffer* GetTransformBuffer() const;
	inline auto& GetTransformManager() const { return m_TransformManager; }
	inline auto& GetMaterialDescriptors() const { return m_MaterialManager->GetDescriptors(); }

	inline const auto& GetDirectMeshes() { return m_DirectMeshes; }

	inline const auto& GetOwnerClusters() { return m_OwnerClusters; }
	inline const auto& GetOrphanClusters() { return m_OrphanClusters; }
	inline const auto& GetSubIndexSegmentClusters() { return m_SubIndexSegmentClusters; }
	inline const auto& GetDirtyClusters() { return m_DirtyClusters; }
	inline const auto& GetAllClusters() { return m_AllClusters; }
	
	// Per-segment cluster helper, called by SubIndexMesh when it creates a SubIndexSegmentMesh child
	// The segment is the unique key into m_SubIndexSegmentClusters
	BLASCluster* GetOrCreateSegmentCluster(SubIndexSegmentMesh* segment, RE::TESObjectREFR* owner);

	// Builds/refits the per-owner BLAS clusters; called from the SceneTLAS pass before the TLAS build.
	void BuildClusters(nvrhi::ICommandList* commandList);

	auto GetMaterial(RE::BSShaderMaterial* shaderMaterial) { return m_MaterialManager->Get(shaderMaterial); }

	inline auto& GetLandLODMeshUpdates() { return m_LandLODMeshUpdates; }

	inline auto& GetLights() { return m_Lights; }
	inline auto& GetLightData() { return m_LightData; }

	inline auto& GetNumMeshesFrame() const { return m_NumMeshes; }
	inline auto& GetNumInstancesFrame() const { return m_NumInstances; }

	inline auto& GetTextureManager() { return m_TextureManager; }

	inline auto& GetCamera() const  { return m_Camera; }

	inline const auto& GetUpdateTimings() const { return m_UpdateTimings; }
	
	void OnDestroy(RE::BSTriShape* bsTriShape);

	void UpdateDynamicData(RE::BSDynamicTriShape* bsDynamicTriShape);

	void Update(nvrhi::ICommandList* commandList);
	void UpdateLights(nvrhi::ICommandList* commandList);

	// Update Camera reference
	void UpdateCamera();

	bool TryMaintenanceRebuild(uint64_t frameIndex);

	void ReleaseTexture(RE::BSGraphics::Texture* texture);
	void MarkClusterDirty(BLASCluster* cluster);

	uint32_t AllocateTransformIndex();
	void WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform);
	
	void ProcessPendingMeshDestroys(uint64_t completedFence);

private:
	eastl::vector<PassTiming> m_UpdateTimings;

	struct PendingDestroy
	{
		eastl::unique_ptr<BaseMesh> mesh;
		uint64_t fenceValue;
	};
	eastl::vector<PendingDestroy> m_PendingMeshDestroy;
};
