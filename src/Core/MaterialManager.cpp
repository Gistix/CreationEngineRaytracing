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
#include "Renderer.h"
#include "Scene.h"

MaterialManager::MaterialManager()
{
	m_Size = kSizeReference * Constants::NUM_MATERIALS_MIN;

	m_Data.resize(m_Size);

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
		.setByteSize(m_Size)
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
	// Recreates the GPU buffer at the current (already grown) m_Size and rebinds it to the
	// descriptor table; the table handle is stable so passes never need to rebind.
	CreateBuffer();

	// The freshly created buffer is empty; restage all live data for re-upload.
	m_DirtyRanges.clear();
	m_DirtyRanges.push_back({ 0, m_NextOffset });
}

uint64_t MaterialManager::Allocate()
{
	if (!m_FreeOffsets.empty()) {
		uint64_t offset = m_FreeOffsets.back();
		m_FreeOffsets.pop_back();
		return offset;
	}

	if (m_NextOffset + kSizeReference > m_Size) {
		// Grow the logical capacity (and CPU mirror) now so Write() stays in-bounds; the GPU
		// buffer itself is recreated lazily in Flush() where a command list is available.
		m_Size += kSizeReference * Constants::NUM_MATERIALS_STEP;
		m_Data.resize(m_Size);
	}

	uint64_t offset = m_NextOffset;
	m_NextOffset += kSizeReference;
	return offset;
}

void MaterialManager::Free(uint64_t offset)
{
	m_FreeOffsets.push_back(offset);
}

void MaterialManager::Write(MaterialBase* material)
{
	const uint64_t offset = material->GetOffset();
	const size_t size = material->GetDataSize();

	if (size > kSizeReference) {
		logger::critical("MaterialManager::Write - material data exceeds the reference slot size");
		return;
	}

	if (offset + size > m_Size) {
		logger::critical("MaterialManager::Write - material data write out of bounds");
		return;
	}

	std::memcpy(m_Data.data() + offset, material->GetData(), size);
	m_DirtyRanges.push_back({ offset, size });
}

eastl::shared_ptr<MaterialBase> MaterialManager::Get(RE::BSShaderMaterial* shaderMaterial)
{
	using Feature = RE::BSShaderMaterial::Feature;
	using Type = RE::BSShaderMaterial::Type;

	std::scoped_lock lock(m_MaterialMutex);

	auto& material = m_Material[shaderMaterial];

	if (material)
		return material;

	auto offset = Allocate();

	auto type = shaderMaterial->GetType();
	if (type == Type::kLighting) 
	{
		switch (shaderMaterial->GetFeature())
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
	else {
		material = eastl::make_shared<MaterialBase>(shaderMaterial, offset);
	}

	Write(material.get());

	return material;
}

void MaterialManager::Release(RE::BSShaderMaterial* shaderMaterial)
{
	std::scoped_lock lock(m_MaterialMutex);

	auto it = m_Material.find(shaderMaterial);
	if (it == m_Material.end())
		return;

	// The last reference is the one held by the manager, so erase it
	if (it->second.unique()) {
		Free(it->second->GetOffset());
		m_Material.erase(it);
	}
}

void MaterialManager::Update(MaterialBase* material)
{
	std::scoped_lock lock(m_MaterialMutex);

	const uint64_t offset = material->GetOffset();
	const size_t size = material->GetDataSize();

	// Skip the upload entirely when the data is identical to what is already staged
	if (std::memcmp(m_Data.data() + offset, material->GetData(), size) == 0)
		return;

	Write(material);
}

void MaterialManager::Flush(nvrhi::ICommandList* commandList)
{
	std::scoped_lock lock(m_MaterialMutex);

	// Recreate the GPU buffer here (not during Allocate) if the logical capacity outgrew it.
	if (m_Buffer->getDesc().byteSize < m_Size)
		Grow();

	for (const auto& [offset, size] : m_DirtyRanges) {
		if (offset + size > m_Buffer->getDesc().byteSize) {
			logger::error("MaterialManager::Flush - Current range {} is greater than buffer size {}.", offset + size, m_Buffer->getDesc().byteSize);
		}

		commandList->writeBuffer(m_Buffer, m_Data.data() + offset, size, offset);
	}

	m_DirtyRanges.clear();
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