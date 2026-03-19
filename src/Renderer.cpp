#include <PCH.h>
#include "Hooks.h"
#include "Renderer.h"
#include "Scene.h"

#include "Pass/GIComposite.h"

#include "Renderer/RenderNode.h"

Renderer::Renderer()
{
	m_RenderGraph = eastl::make_unique<RenderGraph>(this);
}

nvrhi::ITexture* Renderer::GetDepthTexture() {
	if (!m_DepthTexture) {
		auto& depthStencils = RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils;
		m_DepthTexture = ShareTexture(depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture, "Depth", nvrhi::Format::D24S8, nvrhi::ResourceStates::DepthWrite);
	}

	return m_DepthTexture;
}

void Renderer::Initialize(RendererParams rendererParams)
{
	Hooks::InstallD3D11Hooks(rendererParams.d3d11Device);

	// NVRHI Device
	nvrhi::d3d12::DeviceDesc deviceDesc;
	deviceDesc.errorCB = &MessageCallback::GetInstance();
	deviceDesc.pDevice = rendererParams.d3d12Device;
	deviceDesc.pGraphicsCommandQueue = rendererParams.commandQueue;
	deviceDesc.pComputeCommandQueue = rendererParams.computeCommandQueue;
	deviceDesc.pCopyCommandQueue = rendererParams.copyCommandQueue;
	deviceDesc.aftermathEnabled = true;
	deviceDesc.logBufferLifetime = false;

	m_NVRHIDevice = nvrhi::d3d12::createDevice(deviceDesc);

	if (m_Settings.ValidationLayer)
	{
		nvrhi::DeviceHandle nvrhiValidationLayer = nvrhi::validation::createValidationLayer(m_NVRHIDevice);
		m_NVRHIDevice = nvrhiValidationLayer; // make the rest of the application go through the validation layer
	}

	m_NativeD3D11Device = rendererParams.d3d11Device;
	m_NativeD3D12Device = rendererParams.d3d12Device;

	if (m_FormatMapping.empty())
		for (int i = 0; i < (int)nvrhi::Format::COUNT; ++i)
		{
			auto format = (nvrhi::Format)i;

			// This gets the SRV format, but I guess it should work
			auto nativeFormat = nvrhi::d3d12::convertFormat(format);

			m_FormatMapping.emplace(nativeFormat, format);
		}

	m_FrameTimer = GetDevice()->createTimerQuery();
}

void Renderer::InitDefaultTextures()
{
	uint8_t white[] = { 255u, 255u, 255u, 255u };
	uint8_t gray[] = { 128u, 128u, 128u, 255u };
	uint8_t normal[] = { 128u, 128u, 255u, 255u };
	uint8_t black[] = { 0u, 0u, 0u, 0u };
	uint8_t rmaos[] = { 128u, 0u, 255u, 255u };
	uint8_t detail[] = { 63u, 64u, 63u, 255u };

	nvrhi::TextureDesc desc;
	desc.width = 1;
	desc.height = 1;
	desc.mipLevels = 1;
	desc.format = nvrhi::Format::RGBA8_UNORM;

	auto* textureDescriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetTextureDescriptors()->m_DescriptorTable.get();

	desc.debugName = "Default White Texture";
	m_WhiteTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	desc.debugName = "Default Gray Texture";
	m_GrayTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	desc.debugName = "Default Normal Texture";
	m_NormalTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	desc.debugName = "Default Black Texture";
	m_BlackTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	desc.debugName = "Default RMAOS Texture";
	m_RMAOSTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	desc.debugName = "Default Detail Texture";
	m_DetailTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	// Write the textures using a temporary CL
	nvrhi::CommandListHandle commandList = m_NVRHIDevice->createCommandList();
	commandList->open();

	commandList->beginTrackingTextureState(m_WhiteTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_GrayTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_NormalTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_BlackTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_RMAOSTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_DetailTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

	commandList->writeTexture(m_WhiteTexture->texture, 0, 0, white, 4);
	commandList->writeTexture(m_GrayTexture->texture, 0, 0, gray, 4);
	commandList->writeTexture(m_NormalTexture->texture, 0, 0, normal, 4);
	commandList->writeTexture(m_BlackTexture->texture, 0, 0, black, 4);
	commandList->writeTexture(m_RMAOSTexture->texture, 0, 0, rmaos, 4);
	commandList->writeTexture(m_DetailTexture->texture, 0, 0, detail, 4);

	commandList->setPermanentTextureState(m_WhiteTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_GrayTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_NormalTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_BlackTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_RMAOSTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_DetailTexture->texture, nvrhi::ResourceStates::ShaderResource);

	commandList->commitBarriers();

	commandList->close();
	GetDevice()->executeCommandList(commandList);
}

void Renderer::InitGBufferOutput()
{
	m_GBufferOutput = eastl::make_unique<GBufferOutput>();

	auto device = GetDevice();

	nvrhi::TextureDesc desc;
	desc.width = m_RenderSize.x;
	desc.height = m_RenderSize.y;
	desc.initialState = nvrhi::ResourceStates::RenderTarget;
	desc.isRenderTarget = true;
	desc.useClearValue = true;
	desc.clearValue = nvrhi::Color(0.f);
	desc.keepInitialState = true;
	desc.isTypeless = false;
	desc.isUAV = true;
	desc.mipLevels = 1;

	desc.format = nvrhi::Format::R11G11B10_FLOAT;
	desc.debugName = "GBuffer Motion Vectors";
	m_GBufferOutput->motionVectors = device->createTexture(desc);

	desc.format = nvrhi::Format::RGBA16_FLOAT;
	desc.debugName = "GBuffer Albedo";
	m_GBufferOutput->albedo = device->createTexture(desc);

	desc.format = nvrhi::Format::R10G10B10A2_UNORM;
	desc.debugName = "GBuffer Normal/Roughness";
	m_GBufferOutput->normalRoughness = device->createTexture(desc);

	desc.format = nvrhi::Format::RGBA16_FLOAT;
	desc.debugName = "GBuffer Emissive/Metallic";
	m_GBufferOutput->emissiveMetallic = device->createTexture(desc);

	const nvrhi::Format depthFormats[] = {
		nvrhi::Format::D24S8,
		nvrhi::Format::D32S8,
		nvrhi::Format::D32,
		nvrhi::Format::D16 };

	const nvrhi::FormatSupport depthFeatures =
		nvrhi::FormatSupport::Texture |
		nvrhi::FormatSupport::DepthStencil |
		nvrhi::FormatSupport::ShaderLoad;

	desc.format = nvrhi::utils::ChooseFormat(device, depthFeatures, depthFormats, std::size(depthFormats));
	desc.isUAV = false;
	desc.isTypeless = true;
	desc.initialState = nvrhi::ResourceStates::DepthWrite;
	desc.clearValue = nvrhi::Color(1.f);
	desc.debugName = "GBuffer Depth Texture";
	m_GBufferOutput->depth = device->createTexture(desc);
}

void Renderer::InitRR()
{
	m_RayReconstructionInput = eastl::make_unique<RayReconstructionInput>();

	auto device = GetDevice();

	nvrhi::TextureDesc desc;
	desc.width = m_RenderSize.x;
	desc.height = m_RenderSize.y;
	desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
	desc.keepInitialState = true;
	desc.isUAV = true;
	desc.mipLevels = 1;

	desc.format = nvrhi::Format::R11G11B10_FLOAT;
	desc.debugName = "RR Specular Albedo";
	m_RayReconstructionInput->specularAlbedo = device->createTexture(desc);

	desc.format = nvrhi::Format::R32_FLOAT;
	desc.debugName = "RR Specular Hit Distance";
	m_RayReconstructionInput->specularHitDistance = device->createTexture(desc);
}

void Renderer::SetRenderTargets(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, ID3D12Resource* gnmao)
{
	if (!m_RenderTargets)
		m_RenderTargets = eastl::make_unique<RenderTargets>();

	m_RenderTargets->albedo = CreateHandleForNativeTexture(albedo, "Albedo RenderTarget");
	m_RenderTargets->normalRoughness = CreateHandleForNativeTexture(normalRoughness, "Normal Roughness RenderTarget", nvrhi::Format::UNKNOWN, nvrhi::ResourceStates::UnorderedAccess);
	m_RenderTargets->gnmao = CreateHandleForNativeTexture(gnmao, "GNMAO RenderTarget");
}

void Renderer::SetDiffuseAlbedo(ID3D12Resource* diffuseAlbedo)
{
	GetRRInput()->diffuseAlbedo = CreateHandleForNativeTexture(diffuseAlbedo, "Diffuse Albedo RenderTarget", nvrhi::Format::UNKNOWN, nvrhi::ResourceStates::UnorderedAccess);
}

void Renderer::SetResolution(uint2 resolution)
{
	if (m_RenderSize == resolution)
		return;

	m_RenderSize = resolution;

	{
		nvrhi::TextureDesc desc;
		desc.width = m_RenderSize.x;
		desc.height = m_RenderSize.y;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "Main Texture";

		m_MainTexture = m_NVRHIDevice->createTexture(desc);
	}

	m_RenderGraph->ResolutionChanged(m_RenderSize);

	logger::info("Resolution set to {}x{}", resolution.x, resolution.y);
}

uint2 Renderer::GetResolution()
{
	return m_RenderSize;
}

uint2 Renderer::GetDynamicResolution()
{
	return { 
		static_cast<uint32_t>(m_RenderSize.x * m_DynamicResolutionRatio.x),  
		static_cast<uint32_t>(m_RenderSize.y * m_DynamicResolutionRatio.y)
	};
}

void Renderer::SettingsChanged(const Settings& settings)
{
	m_RenderGraph->SettingsChanged(settings);
}

void Renderer::SetCopyTarget(ID3D12Resource* target)
{
	if (target == m_CopyTargetResource)
		return;

	m_CopyTargetResource = target;

	auto targetDesc = target->GetDesc();

	nvrhi::TextureDesc desc{};
	desc.width = static_cast<uint32_t>(targetDesc.Width);
	desc.height = targetDesc.Height;
	desc.format = nvrhi::Format::RGBA16_FLOAT;
	desc.mipLevels = targetDesc.MipLevels;
	desc.arraySize = targetDesc.DepthOrArraySize;
	desc.dimension = nvrhi::TextureDimension::Texture2D;
	desc.initialState = nvrhi::ResourceStates::ShaderResource;
	desc.keepInitialState = true;
	desc.debugName = "Copy Target Texture";

	m_CopyTargetTexture = m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, target, desc);
}

void Renderer::ExecutePasses()
{
	auto* scene = Scene::GetSingleton();

	scene->m_SceneMutex.lock_shared();

	logger::trace("Renderer::ExecutePasses - Begin");

	auto& stateRuntime = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();

	m_DynamicResolutionRatio = { stateRuntime.dynamicResolutionWidthRatio, stateRuntime.dynamicResolutionHeightRatio };

	// Create a command list
	if (!m_CommandList)
		m_CommandList = GetDevice()->createCommandList();

	m_CommandList->open();

	m_CommandList->beginTimerQuery(m_FrameTimer);

	scene->Update(m_CommandList);

	m_RenderGraph->Execute(m_CommandList);

	scene->ClearDirtyStates();

	if (m_CopyTargetTexture) 
	{
		auto region = nvrhi::TextureSlice{ 0, 0, 0, m_RenderSize.x, m_RenderSize.y, 1 };
		m_CommandList->copyTexture(m_CopyTargetTexture, region, m_MainTexture, region);
	}

	m_CommandList->endTimerQuery(m_FrameTimer);

	// Close it
	m_CommandList->close();

	// Execute it
	m_LastSubmittedInstance = GetDevice()->executeCommandList(m_CommandList, nvrhi::CommandQueue::Graphics);

	logger::trace("Renderer::ExecutePasses - End");
}

void Renderer::WaitExecution()
{
	// Wait for the last submitted command list to finish execution before proceeding
	//m_NVRHIDevice->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Graphics, m_LastSubmittedInstance);

	GetDevice()->waitForIdle();

	PostExecution();
}

void Renderer::PostExecution()
{
	if (GetDevice()->pollTimerQuery(m_FrameTimer))
		m_FrameTime = m_NVRHIDevice->getTimerQueryTime(m_FrameTimer) * 1000.0f;

	m_FrameIndex++;

	// Run garbage collection to release resources that are no longer in use
	GetDevice()->runGarbageCollection();

	auto* scene = Scene::GetSingleton();

	scene->GetSceneGraph()->RunGarbageCollection(m_FrameIndex);

	logger::trace("Renderer::ExecutePasses - Post");

	scene->m_SceneMutex.unlock_shared();
}

nvrhi::TextureHandle Renderer::CreateHandleForNativeTexture(ID3D12Resource* nativeResource, const char* debugName, nvrhi::Format format, nvrhi::ResourceStates resourceState)
{
	D3D12_RESOURCE_DESC nativeTexDesc = nativeResource->GetDesc();

	if (format == nvrhi::Format::UNKNOWN)
	{
		auto formatIt = Renderer::GetFormatMapping().find(nativeTexDesc.Format);

		if (formatIt == Renderer::GetFormatMapping().end()) {
			logger::error("Renderer::CreateHandleForNativeTexture - Unmapped format {}", magic_enum::enum_name(nativeTexDesc.Format));
			return nullptr;
		}

		format = formatIt->second;
	}

	auto textureDesc = nvrhi::TextureDesc()
		.setWidth(static_cast<uint32_t>(nativeTexDesc.Width))
		.setHeight(nativeTexDesc.Height)
		.setFormat(format)
		.setKeepInitialState(true)
		.setDebugName(debugName);

	if (resourceState == nvrhi::ResourceStates::Unknown)
		textureDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
	else if (resourceState == nvrhi::ResourceStates::UnorderedAccess) {
		textureDesc.
			setInitialState(nvrhi::ResourceStates::UnorderedAccess).
			setIsUAV(true);
	} else
		textureDesc.setInitialState(resourceState);

	return GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nativeResource, textureDesc);
}

nvrhi::TextureHandle Renderer::ShareTexture(ID3D11Texture2D* d3d11Texture, const char* debugName, nvrhi::Format format = nvrhi::Format::UNKNOWN, nvrhi::ResourceStates resourceState = nvrhi::ResourceStates::Unknown)
{
	D3D11_TEXTURE2D_DESC desc;
	d3d11Texture->GetDesc(&desc);

	winrt::com_ptr<IDXGIResource1> dxgiResource;
	d3d11Texture->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));

	HANDLE sharedHandle = nullptr;

	dxgiResource->GetSharedHandle(&sharedHandle);

	auto* nativeDevice = Renderer::GetSingleton()->GetNativeD3D12Device();
	auto device = Renderer::GetSingleton()->GetDevice();

	winrt::com_ptr<ID3D12Resource> d3d12Resource;
	nativeDevice->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Resource.put()));

	CloseHandle(sharedHandle);

	return CreateHandleForNativeTexture(d3d12Resource.get(), std::format("{} [Shared Texture]", debugName).c_str(), format, resourceState);
}