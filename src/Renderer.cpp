#include <PCH.h>
#include "Hooks.h"
#include "Renderer.h"
#include "Scene.h"

#include <Rtxdi/RtxdiUtils.h>

#include "Renderer/RenderNode.h"

Renderer::Renderer()
{
	m_RenderGraph = eastl::make_unique<RenderGraph>(this);
}

bool Renderer::Initialize(RendererParams rendererParams)
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

	if (!m_NVRHIDevice)
		return false;

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

	return true;
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

#if defined(SKYRIM)
	desc.debugName = "Default RMAOS Texture";
	m_RMAOSTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);
#endif

	desc.debugName = "Default Detail Texture";
	m_DetailTexture = eastl::make_unique<TextureReference>(m_NVRHIDevice->createTexture(desc), textureDescriptorTable);

	// Write the textures using a temporary CL
	nvrhi::CommandListHandle commandList = m_NVRHIDevice->createCommandList();
	commandList->open();

	commandList->beginTrackingTextureState(m_WhiteTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_GrayTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_NormalTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->beginTrackingTextureState(m_BlackTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
#if defined(SKYRIM)
	commandList->beginTrackingTextureState(m_RMAOSTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
#endif
	commandList->beginTrackingTextureState(m_DetailTexture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

	commandList->writeTexture(m_WhiteTexture->texture, 0, 0, white, 4);
	commandList->writeTexture(m_GrayTexture->texture, 0, 0, gray, 4);
	commandList->writeTexture(m_NormalTexture->texture, 0, 0, normal, 4);
	commandList->writeTexture(m_BlackTexture->texture, 0, 0, black, 4);
#if defined(SKYRIM)
	commandList->writeTexture(m_RMAOSTexture->texture, 0, 0, rmaos, 4);
#endif
	commandList->writeTexture(m_DetailTexture->texture, 0, 0, detail, 4);

	commandList->setPermanentTextureState(m_WhiteTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_GrayTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_NormalTexture->texture, nvrhi::ResourceStates::ShaderResource);
	commandList->setPermanentTextureState(m_BlackTexture->texture, nvrhi::ResourceStates::ShaderResource);
#if defined(SKYRIM)
	commandList->setPermanentTextureState(m_RMAOSTexture->texture, nvrhi::ResourceStates::ShaderResource);
#endif
	commandList->setPermanentTextureState(m_DetailTexture->texture, nvrhi::ResourceStates::ShaderResource);

	commandList->commitBarriers();

	commandList->close();
	GetDevice()->executeCommandList(commandList);
}

nvrhi::ITexture* Renderer::GetDepthTexture() {
	if (!m_DepthTexture) {
		auto& depthStencils = RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils;
		m_DepthTexture = ShareTexture(depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture, "Depth", nvrhi::Format::D24S8, nvrhi::ResourceStates::DepthWrite);
	}

	return m_DepthTexture;
}

nvrhi::ITexture* Renderer::GetMotionVectorTexture() {
	if (!m_MotionVectorTexture) {
		auto& renderTargets = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().renderTargets;
		m_MotionVectorTexture = ShareTexture(renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].texture, "Motion Vector", nvrhi::Format::RG16_FLOAT, nvrhi::ResourceStates::ShaderResource);
	}

	return m_MotionVectorTexture;
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

void Renderer::InitStablePlanes()
{
	m_StablePlanes = eastl::make_unique<StablePlanesResources>();

	auto device = GetDevice();
	const uint width = m_RenderSize.x;
	const uint height = m_RenderSize.y;
	constexpr uint stablePlaneCount = 3;

	// StablePlanesHeader: R32_UINT, 2DArray with 4 slices
	// Slices 0-2: BranchIDs per plane, Slice 3: firstHitRayLength | dominantIndex
	{
		nvrhi::TextureDesc desc;
		desc.dimension = nvrhi::TextureDimension::Texture2DArray;
		desc.width = width;
		desc.height = height;
		desc.arraySize = 4;
		desc.format = nvrhi::Format::R32_UINT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "StablePlanesHeader";
		m_StablePlanes->header = device->createTexture(desc);
	}

	// StablePlanesBuffer: StructuredBuffer<StablePlane>, stride=80 bytes, count=3*W*H
	{
		nvrhi::BufferDesc desc;
		desc.byteSize = stablePlaneCount * width * height * 80;
		desc.structStride = 80;
		desc.canHaveUAVs = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "StablePlanesBuffer";
		m_StablePlanes->buffer = device->createBuffer(desc);
	}

	// StableRadiance: RGBA16_FLOAT, 2D - noise-free emissive along delta paths
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "StableRadiance";
		m_StablePlanes->stableRadiance = device->createTexture(desc);
	}

	logger::info("Stable Planes resources created ({}x{}, {} planes)", width, height, stablePlaneCount);
}

void Renderer::InitReSTIRGI()
{
	m_ReSTIRGIResources = eastl::make_unique<ReSTIRGIResources>();

	auto device = GetDevice();
	const uint width = m_RenderSize.x;
	const uint height = m_RenderSize.y;

	// Calculate reservoir buffer sizing using RTXDI block-linear layout
	constexpr uint reservoirStride = 32;
	constexpr uint blockSize = 16; // RTXDI_RESERVOIR_BLOCK_SIZE
	const uint reservoirBlockRowPitch = (width + blockSize - 1) / blockSize;
	const uint reservoirArrayPitch = reservoirBlockRowPitch * ((height + blockSize - 1) / blockSize) * blockSize * blockSize;
	constexpr uint numArrays = 2;

	{
		nvrhi::BufferDesc desc;
		desc.byteSize = reservoirArrayPitch * numArrays * reservoirStride;
		desc.structStride = reservoirStride;
		desc.canHaveUAVs = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Reservoir Buffer";
		m_ReSTIRGIResources->reservoirBuffer = device->createBuffer(desc);
	}

	// Neighbor offset buffer: 8192 pairs of int8 offsets
	constexpr uint neighborOffsetCount = 8192;
	{
		m_ReSTIRGIResources->neighborOffsetData.resize(neighborOffsetCount * 2);
		rtxdi::FillNeighborOffsetBuffer(m_ReSTIRGIResources->neighborOffsetData.data(), neighborOffsetCount);

		nvrhi::BufferDesc desc;
		desc.byteSize = neighborOffsetCount * 2;
		desc.format = nvrhi::Format::RG8_SNORM;
		desc.canHaveTypedViews = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.debugName = "ReSTIR GI Neighbor Offsets";
		m_ReSTIRGIResources->neighborOffsetBuffer = device->createBuffer(desc);
		m_ReSTIRGIResources->needsNeighborOffsetUpload = true;
	}

	// Packed primary surface data: ping-pong StructuredBuffer (2 planes × width × height × 48 bytes)
	{
		constexpr uint surfaceDataStride = 48; // sizeof(PackedSurfaceData)
		nvrhi::BufferDesc desc;
		desc.byteSize = 2u * width * height * surfaceDataStride;
		desc.structStride = surfaceDataStride;
		desc.canHaveUAVs = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Surface Data Buffer";
		m_ReSTIRGIResources->surfaceDataBuffer = device->createBuffer(desc);
	}

	// Secondary G-buffer: position/normal (RGBA32_FLOAT)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA32_FLOAT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Secondary Position/Normal";
		m_ReSTIRGIResources->secondaryGBufferPositionNormal = device->createTexture(desc);
	}

	// Secondary G-buffer: radiance (RGBA32_FLOAT: radiance.xyz + samplePdf)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA32_FLOAT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Secondary Radiance";
		m_ReSTIRGIResources->secondaryGBufferRadiance = device->createTexture(desc);
	}

	// Secondary G-buffer: diffuse albedo (RGBA16_FLOAT)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Secondary Diffuse Albedo";
		m_ReSTIRGIResources->secondaryGBufferDiffuseAlbedo = device->createTexture(desc);
	}

	// Secondary G-buffer: specular F0 + roughness (RGBA16_FLOAT)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Secondary Specular/Roughness";
		m_ReSTIRGIResources->secondaryGBufferSpecularF0Roughness = device->createTexture(desc);
	}

	// Previous frame G-buffer: depth (R32_FLOAT)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::R32_FLOAT;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Prev GBuffer Depth";
		m_ReSTIRGIResources->prevGBufferDepth = device->createTexture(desc);
	}

	// Previous frame G-buffer: normals (RGBA16_SNORM — matches shared normalRoughness texture format)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA16_SNORM;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "ReSTIR GI Prev GBuffer Normals";
		m_ReSTIRGIResources->prevGBufferNormals = device->createTexture(desc);
	}

	logger::info("ReSTIR GI resources created ({}x{})", width, height);
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

void Renderer::SetPTOutputTargets(ID3D12Resource* depthTarget, ID3D12Resource* mvTarget)
{
	if (depthTarget == m_PTDepthCopyTargetResource && mvTarget == m_PTMVCopyTargetResource)
		return;

	if (depthTarget) {
		m_PTDepthCopyTargetResource = depthTarget;
		auto targetDesc = depthTarget->GetDesc();
		nvrhi::TextureDesc desc{};
		desc.width = static_cast<uint32_t>(targetDesc.Width);
		desc.height = targetDesc.Height;
		desc.format = nvrhi::Format::R32_FLOAT;
		desc.mipLevels = targetDesc.MipLevels;
		desc.arraySize = targetDesc.DepthOrArraySize;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = "PT Depth Copy Target";
		m_PTDepthCopyTargetTexture = m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, depthTarget, desc);
	} else {
		m_PTDepthCopyTargetResource = nullptr;
		m_PTDepthCopyTargetTexture = nullptr;
	}

	if (mvTarget) {
		m_PTMVCopyTargetResource = mvTarget;
		auto targetDesc = mvTarget->GetDesc();
		nvrhi::TextureDesc desc{};
		desc.width = static_cast<uint32_t>(targetDesc.Width);
		desc.height = targetDesc.Height;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.mipLevels = targetDesc.MipLevels;
		desc.arraySize = targetDesc.DepthOrArraySize;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = "PT MV Copy Target";
		m_PTMVCopyTargetTexture = m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, mvTarget, desc);
	} else {
		m_PTMVCopyTargetResource = nullptr;
		m_PTMVCopyTargetTexture = nullptr;
	}
}

nvrhi::ICommandList* Renderer::StartExecution()
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

	return m_CommandList;
}

void Renderer::EndExecution()
{
	if (m_CopyTargetTexture)
	{
		auto region = nvrhi::TextureSlice{ 0, 0, 0, m_RenderSize.x, m_RenderSize.y, 1 };
		m_CommandList->copyTexture(m_CopyTargetTexture, region, m_MainTexture, region);

		if (Scene::GetSingleton()->m_Settings.GeneralSettings.Mode == Mode::PathTracing) {
			if (m_PTDepthCopyTargetTexture)
				m_CommandList->copyTexture(m_PTDepthCopyTargetTexture, region, m_TextureManager.GetTexture(TextureManager::Texture::ClipDepth), region);
	
			if (m_PTMVCopyTargetTexture)
				m_CommandList->copyTexture(m_PTMVCopyTargetTexture, region, m_TextureManager.GetTexture(TextureManager::Texture::MotionVectors3D), region);
		}
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

	// Flush pending BLAS compactions before destroying any models,
	// otherwise stale IDs in asBuildsCompleted can reference freed entries in RTXMU
	{
		auto computeCommandList = GetComputeCommandList();
		computeCommandList->open();
		computeCommandList->compactBottomLevelAccelStructs();
		computeCommandList->close();
		GetDevice()->executeCommandList(computeCommandList, nvrhi::CommandQueue::Compute);
	}

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
	if (!d3d11Texture) {
		logger::error("Renderer::ShareTexture - Invalid D3D11 texture pointer");
		return nullptr;
	}

	winrt::com_ptr<IDXGIResource1> dxgiResource;
	d3d11Texture->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));

	HANDLE sharedHandle = nullptr;

	dxgiResource->GetSharedHandle(&sharedHandle);

	auto* nativeDevice = Renderer::GetSingleton()->GetNativeD3D12Device();
	auto device = Renderer::GetSingleton()->GetDevice();

	winrt::com_ptr<ID3D12Resource> d3d12Resource;
	nativeDevice->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Resource.put()));

	if (!d3d12Resource) {
		logger::error("Renderer::ShareTexture - Failed to open shared handle for D3D12 resource");
		return nullptr;
	}

	return CreateHandleForNativeTexture(d3d12Resource.get(), std::format("{} [Shared Texture]", debugName).c_str(), format, resourceState);
}
