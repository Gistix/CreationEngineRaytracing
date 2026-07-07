#include "Core/MaterialTexture.h"
#include "Core/MaterialManager.h"

bool MaterialTexture::Update(const RE::NiSourceTexturePtr& a_sourceTexture, const eastl::shared_ptr<DescriptorHandle> a_defaultDescriptor, TextureType a_type)
{
	auto sourceTexturePtr = a_sourceTexture.get();
	if (sourceTexture == sourceTexturePtr)
		return false;

	texture = MaterialManager::GetTexture(a_sourceTexture, a_defaultDescriptor, a_type);
	sourceTexture = sourceTexturePtr;

	return true;
}