#pragma once

#include "core/Model.h"
#include "core/Instance.h"
#include "core/Light.h"
#include "Core/ActorReference.h"
#include "Core/TextureManager.h"

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
	eastl::unordered_map<RE::NiAVObject*, Instance*> m_InstanceNodes;
	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> m_InstancesFormIDs;

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

	eastl::vector<eastl::unique_ptr<Mesh>> CreateMeshes(RE::TESForm* form, RE::NiAVObject* object);
	uint32_t CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* node);
	bool CommitModel(const char* path, RE::NiAVObject* object, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);
	void AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path);
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

	inline auto& GetInstances() const { return m_Instances; }
	inline auto& GetLights() { return m_Lights; }

	inline auto& GetTextureManager() { return m_TextureManager; }

	void Update(nvrhi::ICommandList* commandList);
	void UpdateLights(nvrhi::ICommandList* commandList);
	void UpdateActors();
	void ClearDirtyStates();

	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateActorModel(RE::Actor* actor, RE::NiAVObject* root = nullptr, bool firstPerson = false);
	void CreateLandModel(RE::TESObjectLAND* land);
	void CreateWaterModel(RE::TESWaterForm* water, RE::NiAVObject* object);
	void CreateLODModel(RE::NiNode* node);

	void ActorEquip(RE::Actor* a_actor, RE::TESForm* a_form, RE::NiAVObject* a_object, eastl::vector<Mesh*>& a_meshes, bool firstPerson);
	void ActorUnequip(RE::Actor* a_actor, const eastl::vector<Mesh*>& a_meshes, bool firstPerson);

	ActorReference* GetActorRefr(RE::FormID a_formID);

	void ReleaseTexture(RE::BSGraphics::Texture* texture);

	// Releases an object instance while keeping the model and mesh data intact.
	// releaseModel is to be used by water and only water.
	void ReleaseObjectInstance(RE::NiAVObject* object, bool releaseModel = false);

	// Releases all instances of a form, and optionally releases the model and mesh data if there are no remaining instances using it.
	void ReleaseFormInstances(RE::TESForm* form, bool releaseModel);

	void SetInstanceDetached(RE::TESForm* form, bool detached);

	void RunGarbageCollection(uint64_t frameIndex);
};