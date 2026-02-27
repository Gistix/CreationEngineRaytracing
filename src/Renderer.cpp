#include <PCH.h>

#include "Renderer.h"
#include "Hooks.h"
#include "Scene.h"

#include "Passes/GIComposite.h"

#include "Renderer/RenderNode.h"

Renderer::Renderer()
{
	m_RenderGraph = eastl::make_unique<RenderGraph>(this);
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

	commandList->writeTexture(m_WhiteTexture->texture, 0, 0, &white, 0);
	commandList->writeTexture(m_GrayTexture->texture, 0, 0, &gray, 0);
	commandList->writeTexture(m_NormalTexture->texture, 0, 0, &normal, 0);
	commandList->writeTexture(m_BlackTexture->texture, 0, 0, &black, 0);
	commandList->writeTexture(m_RMAOSTexture->texture, 0, 0, &rmaos, 0);
	commandList->writeTexture(m_DetailTexture->texture, 0, 0, &detail, 0);

	commandList->setPermanentTextureState(m_WhiteTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_GrayTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_NormalTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_BlackTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_RMAOSTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_DetailTexture->texture, nvrhi::ResourceStates::ShaderResource);

	commandList->commitBarriers();

	commandList->close();
	m_NVRHIDevice->executeCommandList(commandList);
}

void Renderer::InitRenderPasses()
{
	/*auto main = m_FrameGraph.createTexture(nvrhi::TextureDesc()
		.setWidth(m_RenderSize.x)
		.setHeight(m_RenderSize.y)
		.setIsUAV(true)
		.setFormat(nvrhi::Format::RGBA16_FLOAT)
		.setInitialState(nvrhi::ResourceStates::UnorderedAccess)
		.setDebugName("Main Texture")
	);

	auto lighting = m_FrameGraph.createTexture({ ... });

	m_FrameGraph.addPass("Raytracing", 
		[&](PassBuilder& builder)
		{
			builder.read(gbuffer);
			builder.write(lighting);
		},
		[&](PassResources& resources)
		{
			auto gbufferTex = resources.getTexture(gbuffer);
			auto lightingTex = resources.getTexture(lighting);
		});*/

	/*auto main = m_FrameGraph.createTexture(nvrhi::TextureDesc()
		.setWidth(m_RenderSize.x)
		.setHeight(m_RenderSize.y)
		.setIsUAV(true)
		.setFormat(nvrhi::Format::RGBA16_FLOAT)
		.setInitialState(nvrhi::ResourceStates::UnorderedAccess)
		.setDebugName("Main Texture")
	);

	// Temporarily set up a single raytracing pass, more passes will be added later and in a more dynamic way
	m_RenderPasses.emplace_back(eastl::make_unique<RaytracingPass>(this, m_FrameGraph));*/

	m_CommandList = m_NVRHIDevice->createCommandList();

	m_CommandList->open();
}

void Renderer::SetResolution(uint2 resolution)
{
	m_PendingRenderSize = resolution;

	logger::info("Resolution set to {}x{}", resolution.x, resolution.y);
}

uint2 Renderer::GetResolution()
{
	return m_RenderSize;
}

void Renderer::CheckResolutionResources()
{
	if (m_RenderSize == m_PendingRenderSize)
		return;

	m_RenderSize = m_PendingRenderSize;

	// Output Texture
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
	desc.initialState = nvrhi::ResourceStates::Common;
	desc.keepInitialState = true;
	desc.debugName = "Copy Target Texture";

	m_CopyTargetTexture = m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, target, desc);
}

void Renderer::ExecutePasses()
{
	CheckResolutionResources();

	// Get current command list
	auto commandList = GetCommandList();

	commandList->beginTimerQuery(m_FrameTimer);

	logger::trace("Renderer::ExecutePasses - Frame {}", m_FrameIndex);

	Scene::GetSingleton()->Update(commandList);

	m_RenderGraph->Execute(commandList);

	Scene::GetSingleton()->ClearDirtyStates();

	if (m_CopyTargetTexture) 
	{
		auto region = nvrhi::TextureSlice{ 0, 0, 0, m_RenderSize.x, m_RenderSize.y, 1 };
		commandList->copyTexture(m_CopyTargetTexture, region, m_MainTexture, region);
	}

	commandList->endTimerQuery(m_FrameTimer);

	// Close it
	commandList->close();

	// Execute it
	m_LastSubmittedInstance = m_NVRHIDevice->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);

	// Open it again, NVRHI handles multiple command lists internally (or so they say)
	commandList->open();
}

void Renderer::WaitExecution()
{
	// Wait for the last submitted command list to finish execution before proceeding
	//m_NVRHIDevice->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Graphics, m_LastSubmittedInstance);

	m_NVRHIDevice->waitForIdle();

	if (m_NVRHIDevice->pollTimerQuery(m_FrameTimer))
		m_FrameTime = m_NVRHIDevice->getTimerQueryTime(m_FrameTimer) * 1000.0f;

	m_FrameIndex++;

	// Run garbage collection to release resources that are no longer in use
	m_NVRHIDevice->runGarbageCollection();
}

void Renderer::Load()
{

}

void Renderer::PostPostLoad()
{
	Hooks::Install();
}

void Renderer::DataLoaded()
{

}

void Renderer::SetLogLevel(spdlog::level::level_enum a_level)
{
	logLevel = a_level;
	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), magic_enum::enum_integer(logLevel));
}

spdlog::level::level_enum Renderer::GetLogLevel()
{
	return logLevel;
}