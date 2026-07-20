#pragma once

#include "Core/ResourceSlotManager.h"
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

class MaterialManager : public eastl::enable_shared_from_this<MaterialManager>
{
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

	eastl::unordered_map<RE::BSShaderMaterial*, eastl::weak_ptr<MaterialBase>> m_Material;
	mutable std::mutex m_MaterialMutex;

	ResourceSlotManager m_Slots;

	nvrhi::BufferHandle m_Buffer;
	eastl::unique_ptr<BindlessTable> m_Descriptors;

	void CreateBuffer();
	void BindBuffer();
	void Grow();
public:
	MaterialManager();

	inline nvrhi::IBuffer* GetBuffer() const { return m_Buffer; }
	inline auto& GetDescriptors() const { return m_Descriptors; }

	eastl::shared_ptr<MaterialBase> Get(RE::BSShaderMaterial* shaderMaterial);
	void Release(uint64_t offset);

	void Update(MaterialBase* material);
	void Flush(nvrhi::ICommandList* commandList);

	static Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);
};