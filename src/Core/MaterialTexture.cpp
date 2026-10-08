#include "Core/MaterialTexture.h"
#include "Core/MaterialManager.h"

bool MaterialTexture::Update(const RE::NiPointer<RE::NiSourceTexture>& a_sourceTexture, const eastl::shared_ptr<DescriptorHandle> a_defaultDescriptor, TextureType a_type)
{
	auto sourceTexturePtr = reinterpret_cast<RE::NiTexture*>(a_sourceTexture.get());
	if (sourceTexture == sourceTexturePtr)
		return false;

	texture = MaterialManager::GetTexture(a_sourceTexture, a_defaultDescriptor, a_type);
	sourceTexture = sourceTexturePtr;

	return true;
}

bool MaterialTexture::Update(RE::NiTexture* a_sourceTexture, const eastl::shared_ptr<DescriptorHandle> a_defaultDescriptor, TextureType a_type)
{
	if (sourceTexture == a_sourceTexture)
		return false;

	texture = MaterialManager::GetTexture(a_sourceTexture, a_defaultDescriptor, a_type);
	sourceTexture = a_sourceTexture;

	return true;
}
