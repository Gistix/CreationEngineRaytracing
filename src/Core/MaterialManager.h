#pragma once

#include "Core/Texture.h"
#include "Core/TextureManager.h"
#include "Core/Material/MaterialBase.h"
#include "Interop/Material/Skyrim/LightingMaterialData.hlsli"

class MaterialManager
{
	static constexpr auto kSizeReference = sizeof(LightingMaterialData);

	eastl::unordered_map<RE::BSShaderMaterial*, eastl::shared_ptr<MaterialBase>> m_Material;
	mutable std::mutex m_MaterialMutex;

	// Stable GPU material data buffer (materials keep a fixed offset for their lifetime)
	nvrhi::BufferHandle m_Buffer;

	// CPU-side mirror of the GPU buffer; source of truth and previous-state comparison
	eastl::vector<uint8_t> m_Data;

	// Recycled slot offsets and pending GPU upload ranges
	eastl::vector<uint64_t> m_FreeOffsets;
	eastl::vector<eastl::pair<uint64_t, uint64_t>> m_DirtyRanges;

	uint64_t m_Size;
	uint64_t m_NextOffset = 0;

	uint64_t Allocate();
	void Free(uint64_t offset);
	void Write(MaterialBase* material);
public:
	MaterialManager();

	inline nvrhi::IBuffer* GetBuffer() const { return m_Buffer; }

	eastl::shared_ptr<MaterialBase> Get(RE::BSShaderMaterial* shaderMaterial);
	void Release(RE::BSShaderMaterial* shaderMaterial);

	void Update(MaterialBase* material);
	void Flush(nvrhi::ICommandList* commandList);

	static Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);
};