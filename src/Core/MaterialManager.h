#pragma once

#include "Core/Texture.h"
#include "Core/TextureManager.h"
#include "Core/Material/MaterialBase.h"
#include "Interop/Material/Skyrim/LightingMaterialData.hlsli"
#include "Interop/Material/Skyrim/EnvmapMaterialData.hlsli"
#include "Interop/Material/Skyrim/GlowmapMaterialData.hlsli"
#include "Interop/Material/Skyrim/ParallaxMaterialData.hlsli"
#include "Interop/Material/Skyrim/FacegenMaterialData.hlsli"
#include "Interop/Material/Skyrim/FacegenTintMaterialData.hlsli"
#include "Interop/Material/Skyrim/HairTintMaterialData.hlsli"
#include "Interop/Material/Skyrim/ParallaxOccMaterialData.hlsli"
#include "Interop/Material/Skyrim/EyeMaterialData.hlsli"
#include "Interop/Material/Skyrim/MultiLayerParallaxMaterialData.hlsli"
#include "Interop/Material/Skyrim/LandscapeMaterialData.hlsli"
#include "Interop/Material/Skyrim/LODLandscapeMaterialData.hlsli"
#include "Interop/Material/Skyrim/PBRMaterialData.hlsli"
#include "Interop/Material/Skyrim/PBRLandscapeMaterialData.hlsli"
#include "Interop/Material/Skyrim/EffectMaterialData.hlsli"
#include "Interop/Material/Skyrim/WaterMaterialData.hlsli"
#include "Types/BindlessTable.h"

class MaterialManager
{
	// Uniform slot size: the largest material-data struct, so every material fits its slot.
	static constexpr size_t kSizeReference = std::max({
		sizeof(LightingMaterialData),
		sizeof(EnvmapMaterialData),
		sizeof(GlowmapMaterialData),
		sizeof(ParallaxMaterialData),
		sizeof(FacegenMaterialData),
		sizeof(FacegenTintMaterialData),
		sizeof(HairTintMaterialData),
		sizeof(ParallaxOccMaterialData),
		sizeof(EyeMaterialData),
		sizeof(MultiLayerParallaxMaterialData),
		sizeof(LandscapeMaterialData),
		sizeof(LODLandscapeMaterialData),
		sizeof(PBRMaterialData),
		sizeof(PBRLandscapeMaterialData),
		sizeof(EffectMaterialData),
		sizeof(WaterMaterialData)
	});

	eastl::unordered_map<RE::BSShaderMaterial*, eastl::shared_ptr<MaterialBase>> m_Material;
	mutable std::mutex m_MaterialMutex;

	// Stable GPU material data buffer (materials keep a fixed offset for their lifetime)
	nvrhi::BufferHandle m_Buffer;

	// Single-slot bindless table holding m_Buffer; passes bind the table so a future resize
	// only needs to rewrite the descriptor instead of rebuilding every pass binding set.
	eastl::unique_ptr<BindlessTable> m_Descriptors;

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

	// (Re)creates m_Buffer at the current m_Size and points the descriptor table at it.
	void CreateBuffer();

	// Writes m_Buffer into descriptor slot 0; call after creating or resizing the buffer.
	void BindBuffer();

	// Grows the buffer by NUM_MATERIALS_STEP materials and restages all data for re-upload.
	void Grow();
public:
	MaterialManager();

	inline nvrhi::IBuffer* GetBuffer() const { return m_Buffer; }
	inline auto& GetDescriptors() const { return m_Descriptors; }

	eastl::shared_ptr<MaterialBase> Get(RE::BSShaderMaterial* shaderMaterial);
	void Release(RE::BSShaderMaterial* shaderMaterial);

	void Update(MaterialBase* material);
	void Flush(nvrhi::ICommandList* commandList);

	static Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);
};