#pragma once

#include <PCH.h>

#include "Mesh.h"
#include "DirtyFlags.h"

class SceneGraph;

struct Model
{
	eastl::string m_Name;

	eastl::vector<eastl::unique_ptr<Mesh>> meshes;

	nvrhi::rt::AccelStructHandle blas;

	uint64_t m_LastUpdate = 0;

	uint64_t m_LastBLASUpdate = 0;

	Model(eastl::string name, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes);

	nvrhi::rt::AccelStructDesc MakeBLASDesc(bool update);

	void CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList);

	void BuildBLAS(nvrhi::ICommandList* commandList);

	static std::string KeySuffix(RE::NiAVObject* root)
	{
		return std::format("_{:08X}", reinterpret_cast<uintptr_t>(root));
	}

	bool ShouldQueueMSNConversion() const
	{
		for (auto& mesh : meshes) {
			if (mesh->material.shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals))
				return true;
		}

		return false;
	}

	/*D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags() const
	{
		if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
			return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;

		return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS UpdateFlags(bool rebuild) const
	{
		if (rebuild)
			return BuildFlags();

		return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	}*/

	void Update();

	void SetData(MeshData* meshData, uint32_t& index);

	void UpdateBLAS(nvrhi::ICommandList* commandList);

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

	void ClearDirtyState() 
	{ 
		m_DirtyFlags = DirtyFlags::None;
	}

	float3 GetExternalEmittance()
	{
		return m_EmittanceColor ? *m_EmittanceColor : float3(1.0f, 1.0f, 1.0f);
	}
private:
	stl::enumeration<DirtyFlags, uint8_t> m_DirtyFlags = DirtyFlags::None;
	stl::enumeration<Mesh::Flags, uint8_t> meshFlags = Mesh::Flags::None;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	eastl::atomic<int> refCount{ 0 };

	// XEMI - This is used to control window emission in day/night tod
	float3* m_EmittanceColor = nullptr;
};