#pragma once

#include "Core/Texture.h"
#include "Core/TextureManager.h"
#include "Core/Material/MaterialBase.h"

class MaterialManager
{
	eastl::unordered_map<RE::BSShaderMaterial*, eastl::shared_ptr<MaterialBase>> m_Material;
	mutable std::mutex m_MaterialMutex;

	static Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);
public:
	eastl::shared_ptr<MaterialBase> Get(RE::BSShaderMaterial* shaderMaterial);
	void Release(RE::BSShaderMaterial* shaderMaterial);
};