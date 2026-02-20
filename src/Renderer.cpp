#include <PCH.h>

#include "Renderer.h"
#include "Hooks.h"
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

	device = d3d12Device;

	for (size_t i = 0; i < _countof(m_CommandLists); i++)
	{
		m_CommandLists[i] = m_NVRHIDevice->createCommandList();
	}
	
	m_CommandLists[m_CommandListIndex]->open();

	return true;
}

void Renderer::SetResolution(uint2 resolution)
{
	pendingRenderSize = resolution;
}

void Renderer::CheckResolutionResources()
{
	if (renderSize == pendingRenderSize)
		return;

	renderSize = pendingRenderSize;

	// Output Texture
	{
		nvrhi::TextureDesc desc;
		desc.width = renderSize.x;
		desc.height = renderSize.y;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "Main Texture";

		m_MainTexture = m_NVRHIDevice->createTexture(desc);
	}

	for (auto& renderPass : renderPasses)
	{
		renderPass->ResolutionChanged(renderSize);
	}
}

void Renderer::ExecutePasses()
{
	// Get current command list
	auto commandList = GetCommandList();

	// Execute render passes on it
	for (auto& renderPass: renderPasses)
	{
		renderPass->Execute(commandList);
	}
	
	// Close it
	commandList->close();

	// Execute it
	m_LastSubmittedInstance = m_NVRHIDevice->executeCommandList(commandList);

	logger::info("Command list index: {}", m_CommandListIndex);
	logger::info("Submitted command list instance: {}", m_LastSubmittedInstance);

	// Update command list index
	m_CommandListIndex = (m_CommandListIndex + 1) % 2;

	// Open the next command list
	GetCommandList()->open();
}

void Renderer::WaitExecution()
{
	// Wait for the last submitted command list to finish execution before proceeding
	m_NVRHIDevice->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Graphics, m_LastSubmittedInstance);

	// Run garbage collection to release resources that are no longer in use
	m_NVRHIDevice->runGarbageCollection();
}

void Renderer::Load()
{

}

void Renderer::PostPostLoad()
{
	Hooks::Install();

	renderPasses.clear();
	renderPasses.emplace_back(eastl::make_unique<RaytracingPass>());

	for (auto& renderPass : renderPasses)
	{
		renderPass->Init();
	}
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