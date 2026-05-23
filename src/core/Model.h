#pragma once

#include <PCH.h>

#include "Constants.h"
#include "DirtyFlags.h"
#include "Mesh.h"

class SceneGraph;

struct DataParams
{
	bool alreadyUpdated;
	bool hidden;
	uint32_t firstMeshID;
};

struct Model
{
	struct Flags {
		enum Flag
		{
			None = 0,
			BuffersUploaded = 1 << 0,
			BLASBuilt = 1 << 1
		};
	};

	using Flag = Flags::Flag;

	eastl::string m_Name;

	eastl::vector<eastl::unique_ptr<Mesh>> m_Meshes;

	nvrhi::rt::AccelStructHandle m_BLAS;
	
	nvrhi::CommandListHandle m_BufferUploadCommandList;
	nvrhi::EventQueryHandle m_BufferUploadQuery;
	uint64_t m_SubmittedCopyInstance = 0;

	nvrhi::CommandListHandle m_BLASBuildCommandList;
	nvrhi::EventQueryHandle m_BLASBuildQuery;

	uint64_t m_LastUpdate = Constants::INVALID_FRAME_INDEX;

	uint64_t m_LastBLASUpdate = Constants::INVALID_FRAME_INDEX;

	uint64_t m_LastDataUpload = Constants::INVALID_FRAME_INDEX;

	DataParams m_DataParams;

	// Meant to used for the player
	bool m_FirstPerson = false;

	Flag m_Flags = Flags::None;

	Model(eastl::string name, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);

	void UpdateMeshFlags();

	nvrhi::rt::AccelStructDesc MakeBLASDesc(bool update);

	void CreateBuffers(SceneGraph* sceneGraph);

	void BuildBLAS();

	bool IsReady() const;

	void UpdateFlags();

	static std::string KeySuffix(RE::NiAVObject* root)
	{
		return std::format("_{:08X}", reinterpret_cast<uintptr_t>(root));
	}

	bool ShouldQueueMSNConversion() const
	{
		for (auto& mesh : m_Meshes) {
			if (mesh->material->shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals) && mesh->material->shaderFlags.none(RE::BSShaderProperty::EShaderPropertyFlag::kLODLandscape))
				return true;
		}

		return false;
	}

	void Update(RE::NiAVObject* object, bool isPlayer, nvrhi::ICommandList* commandList);

	DataParams GetData(MeshData* meshData, uint32_t& index);

	void UpdateBLAS(nvrhi::ICommandList* commandList);

	void AppendMeshes(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);
	void RemoveMeshes(const eastl::vector<Mesh*>& a_meshes);

	void AddRef()
	{
		refCount.fetch_add(1, eastl::memory_order_relaxed);
	}

	// Returns refCount
	int Release()
	{
		return refCount.fetch_sub(1, eastl::memory_order_acq_rel) - 1;
	}

	// Getters
	auto GetMeshFlags() const
	{
		return meshFlags;
	}

	uint32_t GetShaderTypes() const
	{
		return shaderTypes;
	}

	auto GetFeatures() const
	{
		return features;
	}

	auto GetShaderFlags() const
	{
		return shaderFlags;
	}

	auto GetDirtyFlags() const
	{
		return m_DirtyFlags;
	}

	auto TerrainLODUpdated()
	{
		m_DirtyFlags.set(DirtyFlags::Vertex);
	}

	void ClearDirtyState() 
	{ 
		m_DirtyFlags = DirtyFlags::None;
	}

	float3 GetExternalEmittance()
	{
		return m_EmittanceColor ? *m_EmittanceColor : float3(1.0f, 1.0f, 1.0f);
	}
private:
	stl::enumeration<DirtyFlags> m_DirtyFlags = DirtyFlags::None;
	stl::enumeration<Mesh::Flags> meshFlags = Mesh::Flags::None;
	stl::enumeration<Mesh::Type> m_MeshTypes = Mesh::Type::Default;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	eastl::atomic<int> refCount{ 0 };

	// XEMI - This is used to control window emission in day/night tod
	float3* m_EmittanceColor = nullptr;
};