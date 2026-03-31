#include "Renderer/TextureManager.h"
#include "Renderer.h"

nvrhi::ITexture* TextureManager::GetTexture(Texture texture) {
	auto& textureHandle = m_Textures[static_cast<size_t>(texture)];

	auto* renderer = Renderer::GetSingleton();

	if (!textureHandle) {
		auto resolution = renderer->GetResolution();
		nvrhi::TextureDesc desc{};

		// Set default values
		desc.width = resolution.x;
		desc.height = resolution.y;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.debugName = magic_enum::enum_name(texture);

		switch (texture)
		{
		case TextureManager::Texture::DiffuseRadiance:
			break;
		case TextureManager::Texture::SpecularRadiance:
			break;
		default:
			break;
		}

		textureHandle = renderer->GetDevice()->createTexture(desc);
	}

	return textureHandle.Get();
}
