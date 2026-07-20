#include "Core/MaterialManager.h"

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Core/Material/Skyrim/EnvmapMaterial.h"
#include "Core/Material/Skyrim/GlowmapMaterial.h"
#include "Core/Material/Skyrim/ParallaxMaterial.h"
#include "Core/Material/Skyrim/FacegenMaterial.h"
#include "Core/Material/Skyrim/FacegenTintMaterial.h"
#include "Core/Material/Skyrim/HairTintMaterial.h"
#include "Core/Material/Skyrim/ParallaxOccMaterial.h"
#include "Core/Material/Skyrim/EyeMaterial.h"
#include "Core/Material/Skyrim/MultiLayerParallaxMaterial.h"
#include "Core/Material/Skyrim/LandscapeMaterial.h"
#include "Core/Material/Skyrim/LODLandscapeMaterial.h"
#include "Core/Material/Skyrim/PBRMaterial.h"
#include "Core/Material/Skyrim/PBRLandscapeMaterial.h"
#include "Core/Material/Skyrim/EffectMaterial.h"
#include "Core/Material/Skyrim/WaterMaterial.h"
#include "Renderer.h"
#include "Scene.h"

#include "Types/CommunityShaders/BSLightingShaderMaterialPBR.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBRLandscape.h"

#include <typeinfo>

MaterialManager::MaterialManager()
	: m_Slots(kSizeReference, Constants::NUM_MATERIALS_MIN, Constants::NUM_MATERIALS_STEP)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
	bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
	bindlessLayoutDesc.firstSlot = 0;
	bindlessLayoutDesc.maxCapacity = 1;
	bindlessLayoutDesc.registerSpaces = {
		nvrhi::BindingLayoutItem::RawBuffer_SRV(3)
	};

	m_Descriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);

	CreateBuffer();
}

void MaterialManager::CreateBuffer()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(m_Slots.GetCapacity())
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Material Buffer");

	m_Buffer = device->createBuffer(bufferDesc);

	BindBuffer();
}

void MaterialManager::BindBuffer()
{
	auto device = Renderer::GetSingleton()->GetDevice();
	device->writeDescriptorTable(m_Descriptors->m_DescriptorTable, nvrhi::BindingSetItem::RawBuffer_SRV(0, m_Buffer));
}

void MaterialManager::Grow()
{
	CreateBuffer();

	m_Slots.MarkAllDirty();
}

void MaterialManager::Release(uint64_t offset)
{
	m_Slots.Release(offset);
}

eastl::shared_ptr<MaterialBase> MaterialManager::Get(RE::BSShaderMaterial* shaderMaterial)
{
	using Feature = RE::BSShaderMaterial::Feature;
	using Type = RE::BSShaderMaterial::Type;

	std::scoped_lock lock(m_MaterialMutex);

	eastl::shared_ptr<MaterialBase> material = nullptr;

	bool isEmplaced = false;

	auto it = m_Material.find(shaderMaterial);
	if (it != m_Material.end()) {
		isEmplaced = true;

		material = it->second.lock();

		if (material) {
			// If the hash key is different the original material was released and we're looking at an reused pointer
			if (material->GetHashKey() == shaderMaterial->hashKey)
				return material;
			else
				logger::info("BSShaderMaterial - Pointer {} reused, Hash - old: {}, new: {}", fmt::ptr(shaderMaterial), material->GetHashKey(), shaderMaterial->hashKey);
		}
	}

	auto offset = m_Slots.Allocate();

	auto type = shaderMaterial->GetType();
	if (type == Type::kLighting) 
	{
		if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBRLandscape))
			material = eastl::make_shared<PBRLandscapeMaterial>(shaderMaterial, offset);
		else if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBR))
			material = eastl::make_shared<PBRMaterial>(shaderMaterial, offset);
		else switch (shaderMaterial->GetFeature())
		{
		case RE::BSShaderMaterial::Feature::kEnvironmentMap:
			material = eastl::make_shared<EnvmapMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kGlowMap:
			material = eastl::make_shared<GlowmapMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kParallax:
			material = eastl::make_shared<ParallaxMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kFaceGen:
			material = eastl::make_shared<FacegenMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kFaceGenRGBTint:
			material = eastl::make_shared<FacegenTintMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kHairTint:
			material = eastl::make_shared<HairTintMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kParallaxOcc:
			material = eastl::make_shared<ParallaxOccMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kMultilayerParallax:
			material = eastl::make_shared<MultiLayerParallaxMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kEye:
			material = eastl::make_shared<EyeMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kMultiTexLand:
		case RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend:
			material = eastl::make_shared<LandscapeMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kLODLand:
		case RE::BSShaderMaterial::Feature::kLODLandNoise:
			material = eastl::make_shared<LODLandscapeMaterial>(shaderMaterial, offset);
			break;
		case RE::BSShaderMaterial::Feature::kDefault:
		case RE::BSShaderMaterial::Feature::kTreeAnim:
		case RE::BSShaderMaterial::Feature::kMultiIndexTriShapeSnow:
		case RE::BSShaderMaterial::Feature::kLODObjectsHD:
		default:
			material = eastl::make_shared<LightingMaterial>(shaderMaterial, offset);
			break;
		}
	}
	else if (type == Type::kEffect) {
		material = eastl::make_shared<EffectMaterial>(shaderMaterial, offset);
	}
	else if (type == Type::kWater) {
		material = eastl::make_shared<WaterMaterial>(shaderMaterial, offset);
	}
	else {
		material = eastl::make_shared<MaterialBase>(shaderMaterial, offset);
	}

	material->SetManager(shared_from_this());

	if (isEmplaced)
		it->second = material;
	else
		m_Material.emplace(shaderMaterial, material);

	Update(material.get());

	return material;
}

void MaterialManager::Update(MaterialBase* material)
{
	const uint64_t offset = material->GetOffset();
	const size_t size = material->GetDataSize();

	if (size > kSizeReference) {
		logger::critical("MaterialManager::Write - material data exceeds the reference slot size");
		return;
	}

	if (offset + size > m_Slots.GetCapacity()) {
		logger::critical("MaterialManager::Write - material data write out of bounds");
		return;
	}

	m_Slots.Write(offset, material->GetData(), size);
}

void MaterialManager::Flush(nvrhi::ICommandList* commandList)
{
	if (m_Slots.ConsumeGrowFlag() || m_Buffer->getDesc().byteSize < m_Slots.GetCapacity())
		Grow();

	auto dirtyRanges = m_Slots.ConsumeDirtyRanges();

	for (const auto& [offset, size] : dirtyRanges) {
		if (offset + size > m_Buffer->getDesc().byteSize) {
			logger::error("MaterialManager::Flush - Current range {} is greater than buffer size {}.", offset + size, m_Buffer->getDesc().byteSize);
		}

		commandList->writeBuffer(m_Buffer, static_cast<const uint8_t*>(m_Slots.GetMirror()) + offset, size, offset);
	}
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