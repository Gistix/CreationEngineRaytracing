#pragma once

#include <PCH.h>

#include "Constants.h"
#include "DirtyFlags.h"
#include "Mesh.h"
#include "Types/EnumFlags.h"

class SceneGraph;

struct DataParams
{
	bool alreadyUpdated;
	bool hidden;
	uint32_t firstMeshID;
	uint32_t numMeshes;
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

	enum class Type : uint8_t
	{
		Default,
		Actor
	};

	eastl::string m_Name;
	
	Type m_Type = Type::Default;

	eastl::vector<eastl::unique_ptr<Mesh>> m_Meshes;

	nvrhi::rt::AccelStructHandle m_BLAS;
	
	nvrhi::CommandListHandle m_BufferUploadCommandList;
	nvrhi::EventQueryHandle m_BufferUploadQuery;
	uint64_t m_SubmittedCopyInstance = 0;

	nvrhi::CommandListHandle m_BLASBuildCommandList;
	nvrhi::EventQueryHandle m_BLASBuildQuery;

	uint64_t m_LastUpdate = Constants::INVALID_FRAME_INDEX;

	uint64_t m_LastBLASUpdate = Constants::INVALID_FRAME_INDEX;

	uint32_t m_NumUpdatesSinceRebuild = 0;

	uint64_t m_LastDataUpload = Constants::INVALID_FRAME_INDEX;

	DataParams m_DataParams;

	// Meant to used for the player
	bool m_FirstPerson = false;

	Flag m_Flags = Flags::None;

	Model(eastl::string name, Type type, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);

	void UpdateMeshFlags();

	nvrhi::rt::AccelStructDesc MakeBLASDesc(bool update);

	void CreateBuffers(SceneGraph* sceneGraph);

	void BuildBLAS();

	bool IsReady() const;

	void UpdateFlags();

	static eastl::string KeySuffix(RE::NiAVObject* root)
	{
		auto suffix = std::format("_{:08X}", reinterpret_cast<uintptr_t>(root));
		return eastl::string(suffix.c_str());
	}

	bool ShouldQueueMSNConversion() const
	{
		return false;
	}

	void Update(RE::NiAVObject* object, bool isPlayer, nvrhi::ICommandList* commandList);

	DataParams GetData(MeshData* meshData, uint32_t& index);

	void BuildUpdateBLAS(nvrhi::ICommandList* commandList);

	eastl::unique_ptr<Model> Clone(RE::NiAVObject* node, RE::FormID formID) const;

	void AppendMeshes(SceneGraph* sceneGraph, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);
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

	auto GetMeshTypes() const
	{
		return m_MeshTypes;
	}

	auto GetShaderTypes() const
	{
		return 0;
	}

	auto GetAlphaFlags() const
	{
		return 0;
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
	CESEAdapter::REX::EnumSet<DirtyFlags> m_DirtyFlags = DirtyFlags::None;
	CESEAdapter::REX::EnumSet<Mesh::Flags> meshFlags = Mesh::Flags::None;
	EnumFlags<Mesh::Type> m_MeshTypes = 0;
	CESEAdapter::REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	eastl::atomic<int> refCount{ 0 };

	// XEMI - This is used to control window emission in day/night tod
	float3* m_EmittanceColor = nullptr;
};