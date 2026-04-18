#include "Accumulation.h"
#include "Renderer.h"
#include "Scene.h"
#include "Util.h"

namespace Pass::Common
{
	struct AccumulationConstants
	{
		uint32_t AccumulatedFrames;
		uint32_t _Pad[3];
	};

	Accumulation::Accumulation(Renderer* renderer)
		: RenderPass(renderer)
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void Accumulation::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc bindingLayoutDesc;
		bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		bindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),  // CameraData
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),  // AccumulationConstants
			nvrhi::BindingLayoutItem::Texture_SRV(0),             // CurrentFrame
			nvrhi::BindingLayoutItem::Texture_UAV(0)              // AccumulationBuffer
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(bindingLayoutDesc);
	}

	void Accumulation::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/Accumulation.hlsl", {}, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);

		m_ConstantBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(AccumulationConstants), "Accumulation Constants", 16));
	}

	void Accumulation::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();

		// Create accumulation texture matching the main texture format
		auto resolution = renderer->GetResolution();
		{
			nvrhi::TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = "Accumulation Texture";

			m_AccumulationTexture = renderer->GetDevice()->createTexture(desc);
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, renderer->GetMainTexture()),
			nvrhi::BindingSetItem::Texture_UAV(0, m_AccumulationTexture)
		};

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_AccumulatedFrames = 0;

		m_DirtyBindings = false;
	}

	bool Accumulation::DetectCameraChange() const
	{
		auto* scene = Scene::GetSingleton();
		auto* camera = scene->GetCameraData();

		// Compare position with epsilon (avoid sub-pixel floating-point noise)
		auto posDelta = camera->Position - m_PrevPosition;
		if (abs(posDelta.x) > 1e-4f || abs(posDelta.y) > 1e-4f || abs(posDelta.z) > 1e-4f)
			return true;

		// Compare view orientation
		if (memcmp(&camera->ViewInverse, &m_PrevViewInverse, sizeof(float4x4)) != 0)
			return true;

		// Compare projection (unjittered ViewProj, not ProjInverse which has per-frame TAA jitter)
		if (memcmp(&camera->ViewProj, &m_PrevViewProj, sizeof(float4x4)) != 0)
			return true;

		return false;
	}

	bool Accumulation::DetectSettingsChange(const Settings& settings)
	{
		// Use a simple hash over the settings that affect rendering output
		size_t hash = 0;

		auto hashCombine = [&hash](const void* data, size_t size) {
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; i++)
				hash ^= static_cast<size_t>(bytes[i]) << ((i % sizeof(size_t)) * 8);
		};

		hashCombine(&settings.RaytracingSettings, sizeof(settings.RaytracingSettings));
		hashCombine(&settings.LightingSettings, sizeof(settings.LightingSettings));
		hashCombine(&settings.MaterialSettings, sizeof(settings.MaterialSettings));
		hashCombine(&settings.SHaRCSettings, sizeof(settings.SHaRCSettings));
		hashCombine(&settings.AdvancedSettings, sizeof(settings.AdvancedSettings));
		hashCombine(&settings.WaterSettings, sizeof(settings.WaterSettings));
		hashCombine(&settings.ExperimentalSettings, sizeof(settings.ExperimentalSettings));
		hashCombine(&settings.ReSTIRGI, sizeof(settings.ReSTIRGI));

		if (hash != m_PrevSettingsHash) {
			m_PrevSettingsHash = hash;
			return true;
		}
		return false;
	}

	void Accumulation::SettingsChanged(const Settings& settings)
	{
		if (DetectSettingsChange(settings))
			m_AccumulatedFrames = 0;
	}

	void Accumulation::ResolutionChanged(uint2)
	{
		m_DirtyBindings = true;
		m_AccumulatedFrames = 0;
	}

	void Accumulation::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		if (!m_ComputePipeline || !m_BindingSet)
			return;

		// Detect camera changes and reset if needed
		if (DetectCameraChange())
			m_AccumulatedFrames = 0;

		// Update constant buffer
		AccumulationConstants constants;
		constants.AccumulatedFrames = m_AccumulatedFrames;
		constants._Pad[0] = constants._Pad[1] = constants._Pad[2] = 0;
		commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

		// Dispatch compute shader
		auto resolution = GetRenderer()->GetDynamicResolution();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSet };
		commandList->setComputeState(state);

		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);

		// Copy accumulated result back to MainTexture
		auto fullResolution = GetRenderer()->GetResolution();
		auto copyRegion = nvrhi::TextureSlice{ 0, 0, 0, fullResolution.x, fullResolution.y, 1 };
		commandList->copyTexture(GetRenderer()->GetMainTexture(), copyRegion, m_AccumulationTexture, copyRegion);

		// Update camera state for next frame
		auto* camera = Scene::GetSingleton()->GetCameraData();
		m_PrevViewInverse = camera->ViewInverse;
		m_PrevViewProj = camera->ViewProj;
		m_PrevPosition = camera->Position;

		m_AccumulatedFrames++;
	}
}
