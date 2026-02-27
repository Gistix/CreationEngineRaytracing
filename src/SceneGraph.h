#pragma once

#include "core/Model.h"
#include "core/Instance.h"
#include "core/Light.h"

#include "Light.hlsli"
#include "Mesh.hlsli"
#include "Instance.hlsli"

#include "Constants.h"
#include "Types/BindlessTable.h"
#include "Types/TextureReference.h"

class SceneGraph
{
	eastl::unordered_map<RE::BSDismemberSkinInstance*, eastl::vector<Mesh*>> dismemberReferences;

	// Model Path, Model data ptr
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> m_Models;

	// Root node ptr, Instance data
	eastl::vector<eastl::unique_ptr<Instance>> m_Instances;

	eastl::unordered_map<RE::NiAVObject*, Instance*> m_InstanceNodes;
	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> m_InstancesFormIDs;

	eastl::vector<Light> m_Lights;

	eastl::array<LightData, Constants::NUM_LIGHTS_MAX> m_LightData;
	nvrhi::BufferHandle m_LightBuffer;

	eastl::array<MeshData, Constants::NUM_MESHES_MAX> m_MeshData;
	nvrhi::BufferHandle m_MeshBuffer;

	eastl::array<InstanceData, Constants::NUM_INSTANCES_MAX> m_InstanceData;
	nvrhi::BufferHandle m_InstanceBuffer;

	eastl::unordered_map<ID3D11Texture2D*, eastl::unique_ptr<TextureReference>> textures;

	eastl::deque<eastl::string> m_MSNConvertionQueue;
	
	eastl::unique_ptr<BindlessTable> m_TriangleDescriptors;
	eastl::unique_ptr<BindlessTable> m_VertexDescriptors;
	eastl::unique_ptr<BindlessTable> m_TextureDescriptors;

	void CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* node);
	void AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path);

public:
	void Initialize();

	inline auto& GetTriangleDescriptors() const { return m_TriangleDescriptors; }
	inline auto& GetVertexDescriptors() const { return m_VertexDescriptors; }
	inline auto& GetTextureDescriptors() const { return m_TextureDescriptors; }

	inline auto& GetLightBuffer() const { return m_LightBuffer; }
	inline auto& GetMeshBuffer() const { return m_MeshBuffer; }
	inline auto& GetInstanceBuffer() const { return m_InstanceBuffer; }

	inline auto& GetInstances() const { return m_Instances; }
	inline auto& GetLights() { return m_Lights; }

	void Update(nvrhi::ICommandList* commandList);
	void UpdateLights(nvrhi::ICommandList* commandList);
	void ClearDirtyStates();

	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateActorModel(RE::Actor* actor, const char* name, RE::NiAVObject* root);
	void CreateLandModel(RE::TESObjectLAND* land);

	void AddLight(RE::BSLight* light);
	void RemoveLight(RE::BSLight* light);

	eastl::shared_ptr<DescriptorHandle> GetTextureDescriptor(ID3D11Texture2D* d3d11Texture);
	eastl::shared_ptr<DescriptorHandle> GetMSNormalMapDescriptor(Mesh* mesh, RE::BSGraphics::Texture* texture);
};