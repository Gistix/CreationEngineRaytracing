#include <PCH.h>

#include "Renderer.h"
#include "Hooks.h"
#include "Scene.h"

#include "Passes/RaytracingPass.h"

bool Renderer::Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	// NVRHI Device
	nvrhi::d3d12::DeviceDesc deviceDesc;
	deviceDesc.errorCB = &MessageCallback::GetInstance();
	deviceDesc.pDevice = d3d12Device;
	deviceDesc.pGraphicsCommandQueue = commandQueue;
	deviceDesc.pComputeCommandQueue = computeCommandQueue;
	deviceDesc.pCopyCommandQueue = copyCommandQueue;
	deviceDesc.aftermathEnabled = true;

	m_NVRHIDevice = nvrhi::d3d12::createDevice(deviceDesc);

	if (settings.ValidationLayer)
	{
		nvrhi::DeviceHandle nvrhiValidationLayer = nvrhi::validation::createValidationLayer(m_NVRHIDevice);
		m_NVRHIDevice = nvrhiValidationLayer; // make the rest of the application go through the validation layer
	}

	m_NativeD3D12Device = d3d12Device;

	// Initialize Camera Data Buffer
	{
		m_CameraData = eastl::make_unique<CameraData>();

		m_CameraDataBuffer = m_NVRHIDevice->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(CameraData), "Frame Data", Constants::MAX_CB_VERSIONS));
	}

	// Initialize Render Passes
	{
		// Temporarily set up a single raytracing pass, more passes will be added later and in a more dynamic way
		m_RenderPasses.emplace_back(eastl::make_unique<RaytracingPass>(this));
	}

	m_CommandList = m_NVRHIDevice->createCommandList();

	m_CommandList->open();

	return true;
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

	for (auto& renderPass : m_RenderPasses)
	{
		renderPass->ResolutionChanged(m_RenderSize);
	}
}

void Renderer::UpdateCameraData(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const
{
	m_CameraData->ViewInverse = viewInverse;
	m_CameraData->ProjInverse = projInverse;
	m_CameraData->CameraData = cameraData;
	m_CameraData->NDCToView = NDCToView;
	m_CameraData->Position = position;
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

	Scene::GetSingleton()->Update(commandList);

	// Update camera data buffer
	commandList->writeBuffer(m_CameraDataBuffer, m_CameraData.get(), sizeof(CameraData));

	// Execute render passes on it
	for (auto& renderPass : m_RenderPasses) 
	{
		renderPass->Execute(commandList);
	}

	if (m_CopyTargetTexture) 
	{
		auto region = nvrhi::TextureSlice{ 0, 0, 0, m_RenderSize.x, m_RenderSize.y, 1 };
		commandList->copyTexture(m_CopyTargetTexture, region, m_MainTexture, region);
	}

	// Close it
	commandList->close();

	// Execute it
	m_LastSubmittedInstance = m_NVRHIDevice->executeCommandList(commandList);

	// Open it again, NVRHI handles multiple command lists internally
	commandList->open();
}

void Renderer::WaitExecution()
{
	// Wait for the last submitted command list to finish execution before proceeding
	m_NVRHIDevice->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Graphics, m_LastSubmittedInstance);

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