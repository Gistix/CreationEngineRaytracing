#pragma once

#include "core/Model.h"
#include "core/Instance.h"
#include "core/Light.h"
#include "Core/TextureManager.h"
#include "core/TreeLODInstance.h"

#include "Core/Reference/ActorReference.h"
#include "Core/Reference/LODBlockReference.h"
#include "Core/Reference/TreeLODBlockReference.h"

#include "Light.hlsli"
#include "Mesh.hlsli"
#include "Instance.hlsli"

#include "Constants.h"
#include "Types/BindlessTableManager.h"
#include "Types/BindlessTable.h"
#include "Types/ReleasedData.h"

#include <eastl/vector_set.h>
#include <eastl/unordered_set.h>

#include "Pipeline/MSNConverter.h"

class SceneGraph
{
	std::shared_mutex m_ReleaseDataMutex;

	// Model Path, Model data ptr
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> m_Models;

	eastl::vector<ReleasedData> m_ReleasedData;

	// Root node ptr, Instance data
	eastl::vector<eastl::unique_ptr<Instance>> m_Instances;
	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> m_InstancesFormIDs;

	// Water
	eastl::unordered_map<RE::NiAVObject*, Instance*> m_WaterInstances;

	// LOD
	eastl::unordered_map<RE::BGSObjectBlock*, LODBlockReference> m_ObjectLODInstances;
	eastl::unordered_map<RE::BGSTerrainBlock*, LODBlockReference> m_TerrainLODInstances;
	eastl::unordered_map<RE::BGSDistantTreeBlock*, TreeLODBlockReference> m_TreeLODInstances;

	// Actors
	eastl::unordered_map<RE::FormID, ActorReference> m_Actors;

	eastl::unordered_set<RE::BSLight*> m_TempActiveLights;
	eastl::map<RE::BSLight*, Light> m_Lights;

	eastl::array<LightData, Constants::LIGHTS_MAX> m_LightData;
	nvrhi::BufferHandle m_LightBuffer;

	// Material
	eastl::array<MaterialData, Constants::NUM_MESHES_MAX> m_MaterialData;
	nvrhi::BufferHandle m_MaterialBuffer;

	// Mesh
	eastl::array<MeshData, Constants::NUM_MESHES_MAX> m_MeshData;
	nvrhi::BufferHandle m_MeshBuffer;

	// Instance
	eastl::array<InstanceData, Constants::NUM_INSTANCES_MAX> m_InstanceData;
	nvrhi::BufferHandle m_InstanceBuffer;

	eastl::unique_ptr<TextureManager> m_TextureManager;

	eastl::unique_ptr<BindlessTableManager> m_TriangleDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexDescriptors;

	eastl::unique_ptr<BindlessTable> m_DynamicVertexDescriptors;
	eastl::unique_ptr<BindlessTable> m_SkinningDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexCopyDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexWriteDescriptors;
	eastl::unique_ptr<BindlessTable> m_PrevPositionDescriptors;
	eastl::unique_ptr<BindlessTable> m_PrevPositionWriteDescriptors;

	REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> m_CurrentAccumulator;

	uint32_t m_NumMeshes = 0;
	uint32_t m_NumInstances = 0;

	eastl::vector<eastl::unique_ptr<Mesh>> CreateMeshes(RE::NiAVObject* object, RE::TESForm* form);
	uint32_t CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* node);
	Model* CommitModel(const char* path, RE::NiAVObject* object, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);

	Instance* AddInstanceImpl(RE::NiAVObject* node, Model* model, RE::FormID formID);
	void AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path);
	void AddInstance(RE::FormID formID, RE::NiAVObject* node, Model* path);
	void AddInstance(RE::BGSObjectBlock* block, RE::NiAVObject* node, Model* model);
	void AddInstance(RE::BGSTerrainBlock* block, RE::NiAVObject* node, Model* model);

	void ReleaseInstances(eastl::vector<Instance*>& instances, bool releaseModel);
	void ReleaseInstances(eastl::vector<Instance*>& instances);
public:
	void Initialize();

	inline auto& GetTriangleDescriptors() const { return m_TriangleDescriptors; }
	inline auto& GetVertexDescriptors() const { return m_VertexDescriptors; }
	inline auto& GetTextureDescriptors() const { return m_TextureManager->m_TextureDescriptors; }
	inline auto& GetCubemapDescriptors() const { return m_TextureManager->m_CubemapDescriptors; }
	inline auto& GetDynamicVertexDescriptors() const { return m_DynamicVertexDescriptors; }
	inline auto& GetSkinningDescriptors() const { return m_SkinningDescriptors; }
	inline auto& GetVertexCopyDescriptors() const { return m_VertexCopyDescriptors; }
	inline auto& GetVertexWriteDescriptors() const { return m_VertexWriteDescriptors; }
	inline auto& GetPrevPositionDescriptors() const { return m_PrevPositionDescriptors; }
	inline auto& GetPrevPositionWriteDescriptors() const { return m_PrevPositionWriteDescriptors; }

	inline auto& GetLightBuffer() const { return m_LightBuffer; }
	inline auto& GetMeshBuffer() const { return m_MeshBuffer; }
	inline auto& GetInstanceBuffer() const { return m_InstanceBuffer; }

	inline auto& GetModels() { return m_Models; }
	inline auto& GetInstances() const { return m_Instances; }
	inline auto& GetTerrainLodInstances() const { return m_TerrainLODInstances; }

	inline auto& GetLights() { return m_Lights; }

	inline auto& GetNumMeshesFrame() const { return m_NumMeshes; }
	inline auto& GetNumInstancesFrame() const { return m_NumInstances; }

	inline auto& GetTextureManager() { return m_TextureManager; }

	void Update(nvrhi::ICommandList* commandList);
	void UpdateLights(nvrhi::ICommandList* commandList);

	// Update Actor equipment
	void UpdateActors();

	// Update LOD visibility
	void UpdateLODVisibility();

	void ClearDirtyStates();

	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateActorModel(RE::Actor* actor, RE::NiAVObject* root = nullptr, bool firstPerson = false);
	void CreateLandModel(RE::TESObjectLAND* land);
	void CreateWaterModel(RE::TESWaterForm* water, RE::NiAVObject* object);

	// LOD
	bool CreateLODModel(RE::BGSTerrainBlock* chunk);
	bool CreateLODModel(RE::BGSObjectBlock* chunk);
	bool CreateLODModel(RE::BGSDistantTreeBlock* chunk);

	template <typename T>
	void CreateLODModelImpl(T* chunk, Mesh::Type type);

	void ActorEquip(RE::Actor* a_actor, RE::TESForm* a_form, RE::NiAVObject* a_object, eastl::vector<Mesh*>& a_meshes, bool firstPerson);
	void ActorUnequip(RE::Actor* a_actor, const eastl::vector<Mesh*>& a_meshes, bool firstPerson);

	ActorReference* GetActorRefr(RE::FormID a_formID);

	void ReleaseTexture(RE::BSGraphics::Texture* texture);

	// Releases an object instance while keeping the model and mesh data intact.
	// releaseModel is to be used by water and only water.
	void ReleaseWaterInstance(RE::NiAVObject* object);

	// Releases all instances of a form, and optionally releases the model and mesh data if there are no remaining instances using it.
	void ReleaseInstances(RE::TESForm* form, bool releaseModel);

	// Releases all instances of a terrain block (LOD)
	void ReleaseInstances(RE::BGSTerrainBlock* block);

	// Releases all instances of an object block (LOD)
	void ReleaseInstances(RE::BGSObjectBlock* block);

	void SetInstanceDetached(RE::TESForm* form, bool detached);

	void SetLODDetached(RE::BGSTerrainBlock* block, bool detached);

	void SetLODDetached(RE::BGSObjectBlock* block, bool detached);

	void SetLODDetached(RE::BGSDistantTreeBlock* block, bool detached);

	void RunGarbageCollection(uint64_t frameIndex);
};