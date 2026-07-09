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
	Hooks::InstallD3D11(rendererParams.d3d11Device);

	// NVRHI Device
	nvrhi::d3d12::DeviceDesc deviceDesc;
	deviceDesc.errorCB = &MessageCallback::GetInstance();
	deviceDesc.pDevice = rendererParams.d3d12Device;
	deviceDesc.pGraphicsCommandQueue = rendererParams.commandQueue;
	deviceDesc.pComputeCommandQueue = rendererParams.computeCommandQueue;
	deviceDesc.pCopyCommandQueue = rendererParams.copyCommandQueue;
	deviceDesc.aftermathEnabled = false;
	deviceDesc.logBufferLifetime = false;
#if defined(NVRHI_ENHANCED_BARRIERS)
	deviceDesc.enableEnhancedBarriers = true;
#endif

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

	m_NativeD3D12Device->QueryInterface(m_CompatDevice.put());

	// Map DXGI_FORMAT to NVRHI formats
	if (m_FormatMapping.empty())
		for (int i = 0; i < (int)nvrhi::Format::COUNT; ++i)
		{
			auto format = (nvrhi::Format)i;

			// This gets the SRV format, but I guess it should work
			auto nativeFormat = nvrhi::d3d12::convertFormat(format);

			m_FormatMapping.emplace(nativeFormat, format);
		}

	// This one is quite obvious, but just to be sure...
	if (m_NVRHIDevice->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline))
		m_SupportedFeatures |= SupportedFeatures::Raytracing;

	if (m_NVRHIDevice->queryFeatureSupport(nvrhi::Feature::RayTracingOpacityMicromap))
		m_SupportedFeatures |= SupportedFeatures::OpacityMicroMaps;

	if (m_NVRHIDevice->queryFeatureSupport(nvrhi::Feature::LinearSweptSpheres))
		m_SupportedFeatures |= SupportedFeatures::LinearSweptSpheres;
	
	if (m_NVRHIDevice->queryFeatureSupport(nvrhi::Feature::RayQuery))
		m_SupportedFeatures |= SupportedFeatures::InlineRaytracing;
	
	if (m_NVRHIDevice->queryFeatureSupport(nvrhi::Feature::ShaderExecutionReordering))
		m_SupportedFeatures |= SupportedFeatures::ShaderExecutionReordering;

#if defined(NVRHI_ENHANCED_BARRIERS)
	if (m_NVRHIDevice->queryFeatureSupport(nvrhi::Feature::EnhancedBarriers))
		m_SupportedFeatures |= SupportedFeatures::EnhancedBarriers;
#endif

	logger::info("Supported Features: {}", Util::GetFlagsString<SupportedFeatures>(m_SupportedFeatures));

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

	auto desc = nvrhi::TextureDesc()
		.setWidth(1)
		.setHeight(1)
		.setMipLevels(1)
		.setFormat(nvrhi::Format::RGBA8_UNORM)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::Common);

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
	nvrhi::CommandListHandle commandList = GetGraphicsCommandList();
	commandList->open();

	commandList->writeTexture(m_WhiteTexture->texture, 0, 0, white, 4);
	commandList->writeTexture(m_GrayTexture->texture, 0, 0, gray, 4);
	commandList->writeTexture(m_NormalTexture->texture, 0, 0, normal, 4);
	commandList->writeTexture(m_BlackTexture->texture, 0, 0, black, 4);
#if defined(SKYRIM)
	commandList->writeTexture(m_RMAOSTexture->texture, 0, 0, rmaos, 4);
#endif
	commandList->writeTexture(m_DetailTexture->texture, 0, 0, detail, 4);

	commandList->close();

	{
		std::scoped_lock lock(m_ExecutionMutex);
		GetDevice()->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);
	}
}

nvrhi::ITexture* Renderer::GetDepthTexture() {
	if (!m_DepthTexture) {
		auto* d3d11Texture = Util::Adapter::GetMainDepthStencilTexture();
		m_DepthTexture = ShareTexture(d3d11Texture, "Depth", nvrhi::Format::D24S8, nvrhi::ResourceStates::Common);
	}

	return m_DepthTexture;
}

nvrhi::ITexture* Renderer::GetMotionVectorTexture() {
#if defined(SKYRIM)
	if (!m_MotionVectorTexture) {
		auto& renderTargets = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().renderTargets;
		m_MotionVectorTexture = ShareTexture(renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].texture, "Motion Vector", nvrhi::Format::RG16_FLOAT, nvrhi::ResourceStates::ShaderResource);
	}
#endif
	return m_MotionVectorTexture;
}

nvrhi::ITexture* Renderer::GetWaterDisplacementTexture() {
#if defined(SKYRIM)
	if (!m_WaterDisplacementTexture) {
		auto& renderTargets = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().renderTargets;
		m_WaterDisplacementTexture = ShareTexture(renderTargets[RE::RENDER_TARGETS::kWATER_DISPLACEMENT].texture, "Water Displacement", nvrhi::Format::RGBA16_FLOAT, nvrhi::ResourceStates::ShaderResource);
	}
#endif
	return m_WaterDisplacementTexture;
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

	// Packed primary surface data: ping-pong StructuredBuffer (2 planes × width × height × 52 bytes)
	{
		constexpr uint surfaceDataStride = 52; // sizeof(PackedSurfaceData)
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

void Renderer::SetRenderTargets(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, [[maybe_unused]] ID3D12Resource* gnmao)
{
	if (!m_RenderTargets)
		m_RenderTargets = eastl::make_unique<RenderTargets>();

	m_RenderTargets->albedo = CreateHandleForNativeTexture(albedo, "Albedo RenderTarget");
	m_RenderTargets->normalRoughness = CreateHandleForNativeTexture(normalRoughness, "Normal Roughness RenderTarget", nvrhi::Format::UNKNOWN, nvrhi::ResourceStates::UnorderedAccess);
#if defined(SKYRIM)
	m_RenderTargets->gnmao = CreateHandleForNativeTexture(gnmao, "GNMAO RenderTarget");
#endif
}

void Renderer::SetResolution(uint2 resolution)
{
	if (m_RenderSize == resolution)
		return;

	m_RenderSize = resolution;

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

nvrhi::ICommandList* Renderer::StartExecution()
{
	logger::trace("Renderer::StartExecution - Begin (Slot {})", m_NextSlot);

	m_DynamicResolutionRatio = Util::Adapter::GetDynamicResolutionRatios();

	m_CurrentSlot = m_NextSlot;

	auto device = GetDevice();

	auto& slot = m_FrameSlots[m_CurrentSlot];

	if (slot.inFlight) {
		device->waitEventQuery(slot.eventQuery);
		RunPostExecutionForSlot(m_CurrentSlot);
		device->resetEventQuery(slot.eventQuery);
		slot.inFlight = false;
	}

	// Release meshes whose recorded fence has been passed by the GPU.
	Scene::GetSingleton()->GetSceneGraph()->ProcessPendingMeshDestroys(slot.fenceValue);

	if (!slot.eventQuery)
		slot.eventQuery = device->createEventQuery();

	m_FrameIndex++;

	slot.commandList = GetGraphicsCommandList();
	slot.commandList->open();

	m_CommandList = slot.commandList;

	return m_CommandList;
}

void Renderer::EndExecution()
{
	m_CommandList->close();

	auto device = GetDevice();

	uint64_t fenceValue;
	{
		std::scoped_lock lock(m_ExecutionMutex);
		fenceValue = device->executeCommandList(m_CommandList, nvrhi::CommandQueue::Graphics);
		m_LastSubmittedInstance = fenceValue;
	}

	auto& slot = m_FrameSlots[m_CurrentSlot];
	slot.fenceValue = fenceValue;
	device->setEventQuery(slot.eventQuery, nvrhi::CommandQueue::Graphics, fenceValue);
	slot.inFlight = true;

	m_NextSlot = (m_CurrentSlot + 1) % Constants::MAX_FRAMES_IN_FLIGHT;

	logger::trace("Renderer::EndExecution - Slot {} submitted (fence {}), next slot will be {}", m_CurrentSlot, fenceValue, m_NextSlot);
}

uint32_t Renderer::PostExecution()
{
	auto& slot = m_FrameSlots[m_CurrentSlot];

	if (!slot.inFlight)
		return m_LastCompletedSlot;

	if (GetDevice()->pollEventQuery(slot.eventQuery)) {
		RunPostExecutionForSlot(m_CurrentSlot);
		slot.inFlight = false;
	}

	return m_LastCompletedSlot;
}

void Renderer::RunPostExecutionForSlot(uint32_t slot)
{
	auto device = GetDevice();
	auto* scene = Scene::GetSingleton();
	const auto timings = scene->m_Settings.DebugSettings.Timings;

	m_PassTimings.clear();

	if (timings) {
		if (auto* rootNode = m_RenderGraph->GetRootNode()) {
			rootNode->ForEach([&](RenderNode* node) {
				if (node->m_TimerQueries[slot] && device->pollTimerQuery(node->m_TimerQueries[slot]))
					m_PassTimings.push_back(PassTiming{ node->m_Name.c_str(), device->getTimerQueryTime(node->m_TimerQueries[slot]) * 1000.0f, node->m_CpuTimes[slot] });
			});
		}

		if (m_FrameTimerQueries[slot] && device->pollTimerQuery(m_FrameTimerQueries[slot]))
			m_PassTimings.push_back(PassTiming{ "Total", device->getTimerQueryTime(m_FrameTimerQueries[slot]) * 1000.0f, m_FrameCpuTimes[slot] });
	}

	m_LastCompletedSlot = slot;

	device->runGarbageCollection();

	logger::trace("Renderer::RunPostExecutionForSlot - Slot {} completed", slot);
}

nvrhi::TextureHandle Renderer::CreateHandleForNativeTexture(ID3D12Resource* nativeResource, const char* debugName, nvrhi::Format format, nvrhi::ResourceStates resourceState)
{
	D3D12_RESOURCE_DESC nativeTexDesc = nativeResource->GetDesc();

	if (format == nvrhi::Format::UNKNOWN)
	{
		format = Renderer::GetFormat(nativeTexDesc.Format);
		if (format == nvrhi::Format::UNKNOWN) {
			logger::error("Renderer::CreateHandleForNativeTexture - Unmapped format {}", magic_enum::enum_name(nativeTexDesc.Format));
			return nullptr;
		}
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

nvrhi::TextureHandle Renderer::ShareTexture(ID3D11Texture2D* d3d11Texture, const char* debugName, nvrhi::Format format, nvrhi::ResourceStates resourceState)
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

	winrt::com_ptr<ID3D12Resource> d3d12Resource;
	nativeDevice->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Resource.put()));

	if (!d3d12Resource) {
		logger::error("Renderer::ShareTexture - Failed to open shared handle for D3D12 resource: {}", debugName);
		return nullptr;
	}

	return CreateHandleForNativeTexture(d3d12Resource.get(), std::format("{} [Shared Texture]", debugName).c_str(), format, resourceState);
}
