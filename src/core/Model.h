#pragma once

#include <PCH.h>

#include "Mesh.h"

struct Model
{
	eastl::vector<eastl::unique_ptr<Mesh>> meshes;

	nvrhi::rt::AccelStructDesc blasDesc;

	nvrhi::rt::AccelStructHandle blas;

	Model(eastl::vector<eastl::unique_ptr<Mesh>>& meshes) :
		meshes(eastl::move(meshes))
	{
		for (auto& mesh : this->meshes) {
			flags.set(mesh->flags.get());
			shaderTypes |= mesh->material.shaderType;
			features |= static_cast<int>(mesh->material.Feature);
			shaderFlags.set(mesh->material.shaderFlags.get());
		}
	}

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

	void ConvertMSN();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags() const
	{
		if (flags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
			return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;

		return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS UpdateFlags(bool rebuild) const
	{
		if (rebuild)
			return BuildFlags();

		return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	}

	void BuildBLAS(nvrhi::ICommandList* commandList);

	bool UpdateBLAS();

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
	auto GetFlags() const
	{
		return flags;
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

private:
	stl::enumeration<Mesh::Flags, uint8_t> flags = Mesh::Flags::None;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	eastl::atomic<int> refCount{ 0 };
};