#include "RenderTargetManager.h"
#include "Renderer.h"

nvrhi::ITexture* RenderTargetManager::GetTexture(Texture texture) {
	auto& textureHandle = m_Textures[static_cast<size_t>(texture)];

	if (!textureHandle) {
		auto* renderer = Renderer::GetSingleton();

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
		case RenderTarget::ViewDepth:
		case RenderTarget::ClipDepth:
			desc.format = nvrhi::Format::R32_FLOAT;
			break;
		case RenderTarget::FaceNormals:
			desc.format = nvrhi::Format::R11G11B10_FLOAT;
			break;
		case RenderTarget::MotionVectors3D:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			break;
		case RenderTarget::DiffuseRadiance:
		case RenderTarget::SpecularRadiance:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			break;
		default:
			break;
		}

		textureHandle = renderer->GetDevice()->createTexture(desc);
	}

	return textureHandle.Get();
}
