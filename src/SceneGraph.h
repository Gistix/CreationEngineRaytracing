#pragma once

#include "Core/BaseMesh.h"
#include "Core/BLASCluster.h"

#include "core/Model.h"
#include "core/Light.h"
#include "Core/TextureManager.h"

#if defined(SKYRIM)
#include "core/TreeLODInstance.h"
#include "core/GrassInstance.h"
#endif

#include "Core/Reference/ObjectLODBlockReference.h"
#include "Core/Reference/TerrainLODBlockReference.h"
#if defined(SKYRIM)
#include "Core/Reference/TreeLODBlockReference.h"
#include "Core/Reference/GrassReference.h"
#endif
#include "Light.hlsli"
#include "Mesh.hlsli"
#include "Instance.hlsli"

#include "Constants.h"
#include "Types/BindlessTableManager.h"
#include "Types/BindlessTable.h"
#include "Types/VectorStorage.h"
#include "Types/ReleasedData.h"
#include "Types/RE/RE.h"

#include <eastl/vector_set.h>
#include <eastl/unordered_set.h>

#include "Pipeline/MSNConverter.h"

class SceneGraph
{
	RE::NiCamera* m_Camera = nullptr;

	eastl::unordered_map<RE::BSTriShape*, eastl::shared_ptr<BaseMesh>> m_DirectMeshes;
	mutable std::mutex m_MeshMutex;

	// One BLAS/TLAS instance per owner reference; meshes without an owner get a degenerate per-mesh cluster.
	eastl::unordered_map<RE::TESObjectREFR*, eastl::unique_ptr<BLASCluster>> m_OwnerClusters;
	eastl::unordered_map<RE::BSTriShape*, eastl::unique_ptr<BLASCluster>> m_OrphanClusters;

	eastl::vector<RE::BSTriShape*> m_DestroyedMeshes;
	mutable std::mutex m_MeshDestroyMutex;

	// Model Path, Model data ptr
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> m_Models;
	mutable std::mutex m_ModelMutex;

	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> m_InstancesFormIDs;

	// Water
	eastl::unordered_map<RE::NiAVObject*, Instance*> m_WaterInstances;

	// LOD
	eastl::unordered_map<RE::BGSObjectBlock*, eastl::unique_ptr<ObjectLODBlockReference>> m_ObjectLODInstances;
	eastl::unordered_map<RE::BGSTerrainBlock*, eastl::unique_ptr<TerrainLODBlockReference>> m_TerrainLODInstances;
#if defined(SKYRIM)
	eastl::unordered_map<RE::BGSDistantTreeBlock*, eastl::unique_ptr<TreeLODBlockReference>> m_TreeLODInstances;

	// Grass
	eastl::unordered_map<RE::GrassTypeKey, GrassReference> m_GrassInstances;
#endif

	eastl::unordered_set<RE::BSLight*> m_TempActiveLights;
	eastl::map<RE::BSLight*, Light> m_Lights;

	eastl::array<LightData, Constants::LIGHTS_MAX> m_LightData;
	nvrhi::BufferHandle m_LightBuffer;

	// Mesh
	eastl::array<MeshData, Constants::NUM_MESHES_MAX> m_MeshData;
	nvrhi::BufferHandle m_MeshBuffer;

	// Instance
	eastl::array<InstanceData, Constants::NUM_INSTANCES_MAX> m_InstanceData;
	nvrhi::BufferHandle m_InstanceBuffer;

	eastl::unique_ptr<TextureManager> m_TextureManager;

	eastl::unique_ptr<BindlessTableManager> m_TriangleDescriptors;
	eastl::unique_ptr<BindlessTableManager> m_VertexDescriptors;
	eastl::unique_ptr<BindlessTable> m_MaterialDescriptors;
	
	eastl::unique_ptr<BindlessTableManager> m_DynamicVertexDescriptors;
	eastl::unique_ptr<BindlessTable> m_SkinningDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexCopyDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexWriteDescriptors;
	eastl::unique_ptr<BindlessTable> m_DynamicVertexWriteDescriptors;
	eastl::unique_ptr<BindlessTable> m_PrevPositionDescriptors;
	eastl::unique_ptr<BindlessTable> m_PrevPositionWriteDescriptors;

	uint32_t m_NumMeshes = 0;
	uint32_t m_NumInstances = 0;

	uint64_t m_LastMaintenanceFrame = Constants::INVALID_FRAME_INDEX;
	uint32_t m_MaintenanceRebuildsThisFrame = 0;

	// Mesh helpers: route meshes into per-owner BLAS clusters (owner pointer used as key only).
	BLASCluster* GetOrCreateCluster(RE::TESObjectREFR* owner, RE::BSTriShape* bsTriShape);
	void RemoveMeshFromCluster(BaseMesh* mesh, RE::TESObjectREFR* owner);

	// Captures the owner/mesh transforms during traversal (while alive) into the mesh + its cluster.
	void UpdateMeshTransforms(BaseMesh* mesh, RE::TESObjectREFR* owner, RE::BSTriShape* bsTriShape);
public:
	void Initialize();

	inline auto& GetTriangleDescriptors() const { return m_TriangleDescriptors; }
	inline auto& GetVertexDescriptors() const { return m_VertexDescriptors; }
	inline auto& GetMaterialDescriptors() const { return m_MaterialDescriptors; }
	inline auto& GetTextureDescriptors() const { return m_TextureManager->m_TextureDescriptors; }
	inline auto& GetCubemapDescriptors() const { return m_TextureManager->m_CubemapDescriptors; }
	inline auto& GetDynamicVertexDescriptors() const { return m_DynamicVertexDescriptors; }
	inline auto& GetSkinningDescriptors() const { return m_SkinningDescriptors; }
	inline auto& GetVertexCopyDescriptors() const { return m_VertexCopyDescriptors; }
	inline auto& GetVertexWriteDescriptors() const { return m_VertexWriteDescriptors; }
	inline auto& GetDynamicVertexWriteDescriptors() const { return m_DynamicVertexWriteDescriptors; }
	inline auto& GetPrevPositionDescriptors() const { return m_PrevPositionDescriptors; }
	inline auto& GetPrevPositionWriteDescriptors() const { return m_PrevPositionWriteDescriptors; }

	inline auto& GetLightBuffer() const { return m_LightBuffer; }
	inline auto& GetMeshBuffer() const { return m_MeshBuffer; }
	inline auto& GetInstanceBuffer() const { return m_InstanceBuffer; }

	inline auto& GetDirectMeshes() { return m_DirectMeshes; }

	inline auto& GetOwnerClusters() { return m_OwnerClusters; }
	inline auto& GetOrphanClusters() { return m_OrphanClusters; }

	// Builds/refits the per-owner BLAS clusters; called from the SceneTLAS pass before the TLAS build.
	void BuildClusters(nvrhi::ICommandList* commandList);

	inline auto& GetModels() { return m_Models; }
	inline auto& GetTerrainLodInstances() const { return m_TerrainLODInstances; }

	inline auto& GetLights() { return m_Lights; }

	inline auto& GetNumMeshesFrame() const { return m_NumMeshes; }
	inline auto& GetNumInstancesFrame() const { return m_NumInstances; }

	inline auto& GetTextureManager() { return m_TextureManager; }

	inline auto& GetCamera() const  { return m_Camera; }
	
	void OnDestroy(RE::BSTriShape* bsTriShape);

	void UpdateDynamicData(RE::BSDynamicTriShape* bsDynamicTriShape);

	void Update(nvrhi::ICommandList* commandList);
	void UpdateLights(nvrhi::ICommandList* commandList);

	// Update Camera reference
	void UpdateCamera();

	// Update LOD visibility
	void UpdateLODVisibility();

	bool TryMaintenanceRebuild(uint64_t frameIndex);

	void ReleaseTexture(RE::BSGraphics::Texture* texture);
};