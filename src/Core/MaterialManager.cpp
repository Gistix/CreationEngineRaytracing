#include "Core/MaterialManager.h"

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Renderer.h"
#include "Scene.h"

MaterialManager::MaterialManager()
{
	m_Size = kSizeReference * Constants::NUM_MATERIALS_MIN;

	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(m_Size)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Material Buffer");

	Renderer::GetSingleton()->GetDevice()->createBuffer(bufferDesc);
}

eastl::shared_ptr<MaterialBase> MaterialManager::Get(RE::BSShaderMaterial* shaderMaterial)
{
	using Feature = RE::BSShaderMaterial::Feature;
	using Type = RE::BSShaderMaterial::Type;

	logger::info("BSShaderMaterial: {}, Feature: {}, Type: {}",
		fmt::ptr(shaderMaterial),
		magic_enum::enum_name(shaderMaterial->GetFeature()),
		magic_enum::enum_name(shaderMaterial->GetType()));

	std::scoped_lock lock(m_MaterialMutex);

	auto& material = m_Material[shaderMaterial];

	if (material)
		return material;

	auto offset = 0;

	auto type = shaderMaterial->GetType();
	if (type == Type::kLighting) 
	{
		switch (shaderMaterial->GetFeature())
		{
		case RE::BSShaderMaterial::Feature::kNone:
		case RE::BSShaderMaterial::Feature::kDefault:
		case RE::BSShaderMaterial::Feature::kEnvironmentMap:
		case RE::BSShaderMaterial::Feature::kGlowMap:
		case RE::BSShaderMaterial::Feature::kParallax:
		case RE::BSShaderMaterial::Feature::kFaceGen:
		case RE::BSShaderMaterial::Feature::kFaceGenRGBTint:
		case RE::BSShaderMaterial::Feature::kHairTint:
		case RE::BSShaderMaterial::Feature::kParallaxOcc:
		case RE::BSShaderMaterial::Feature::kMultiTexLand:
		case RE::BSShaderMaterial::Feature::kLODLand:
		case RE::BSShaderMaterial::Feature::kUnknown:
		case RE::BSShaderMaterial::Feature::kMultilayerParallax:
		case RE::BSShaderMaterial::Feature::kTreeAnim:
		case RE::BSShaderMaterial::Feature::kMultiIndexTriShapeSnow:
		case RE::BSShaderMaterial::Feature::kLODObjectsHD:
		case RE::BSShaderMaterial::Feature::kEye:
		case RE::BSShaderMaterial::Feature::kCloud:
		case RE::BSShaderMaterial::Feature::kLODLandNoise:
		case RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend:
		default:
			material = eastl::make_shared<LightingMaterial>(shaderMaterial, offset);
			break;
		}
	}
	else {
		material = eastl::make_shared<MaterialBase>(shaderMaterial, offset);
	}



	return material;
}

void MaterialManager::Release(RE::BSShaderMaterial* shaderMaterial)
{
	std::scoped_lock lock(m_MaterialMutex);

	auto it = m_Material.find(shaderMaterial);
	if (it == m_Material.end())
		return;

	// The last reference is the one held by the manager, so erase it
	if (it->second.unique())
		m_Material.erase(it);
}

Texture MaterialManager::GetTexture([[maybe_unused]] const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, [[maybe_unused]] TextureType textureType)
{
#if defined(SKYRIM)
	if (!niPointer || !niPointer->rendererTexture)
		return Texture(defaultDescHandle, nullptr);

	auto& textureManager = Scene::GetSingleton()->GetSceneGraph()->GetTextureManager();

	if (auto result = textureManager->GetDescriptor(niPointer->rendererTexture, textureType))
		return Texture(result, defaultDescHandle.get());
#endif
	return Texture(defaultDescHandle, nullptr);

}