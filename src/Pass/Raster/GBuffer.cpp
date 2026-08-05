#include "GBuffer.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Raster
{
	GBuffer::GBuffer(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_RaytracingData = eastl::make_unique<RaytracingData>();

		m_RaytracingBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(RaytracingData), "Raytracing Data", Constants::MAX_CB_VERSIONS));

		m_LinearWrapSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_LinearClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_PointWrapSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(false));

		m_IndirectArgsBuffer = renderer->GetDevice()->createBuffer(nvrhi::BufferDesc()
			.setByteSize(Constants::NUM_MESHES_MAX * sizeof(nvrhi::DrawIndirectArguments))
			.setStructStride(sizeof(nvrhi::DrawIndirectArguments))
			.setCanHaveUAVs(true)
			.setIsDrawIndirectArgs(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::IndirectArgument)
			.setDebugName("GBuffer Indirect Args"));
	}

	void GBuffer::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_FrameBuffer = nullptr;
		m_GraphicsPipeline = nullptr;
	}

	void GBuffer::Initialize()
	{
		CreatePipeline();
	}

	void GBuffer::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc graphicsLayoutDesc;
		graphicsLayoutDesc.visibility = nvrhi::ShaderType::All;
		graphicsLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
			nvrhi::BindingLayoutItem::RawBuffer_SRV(3),
			nvrhi::BindingLayoutItem::RawBuffer_SRV(4),
			nvrhi::BindingLayoutItem::Texture_SRV(5),
			nvrhi::BindingLayoutItem::Texture_SRV(6),
			nvrhi::BindingLayoutItem::Texture_SRV(7),
			nvrhi::BindingLayoutItem::Texture_SRV(8),
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2)
		};

		m_GraphicsBindingLayout = device->createBindingLayout(graphicsLayoutDesc);

		nvrhi::BindingLayoutDesc argsLayoutDesc;
		argsLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		argsLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::PushConstants(0, sizeof(uint32_t)),
			nvrhi::BindingLayoutItem::RawBuffer_SRV(0),        // MeshSlotRemap
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1), // Meshes
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)  // IndirectArgs
		};

		m_ArgsBindingLayout = device->createBindingLayout(argsLayoutDesc);

		eastl::vector<DxcDefine> defines = { { L"RASTER", L"" } };

		winrt::com_ptr<IDxcBlob> vertexBlob, pixelBlob;
		ShaderUtils::CompileShader(vertexBlob, L"data/shaders/GBufferRaster.hlsl", defines, L"vs_6_5", L"MainVS");
		ShaderUtils::CompileShader(pixelBlob, L"data/shaders/GBufferRaster.hlsl", defines, L"ps_6_5", L"MainPS");

		m_VertexShader = device->createShader({ nvrhi::ShaderType::Vertex, "", "MainVS" }, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize());
		m_PixelShader = device->createShader({ nvrhi::ShaderType::Pixel, "", "MainPS" }, pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize());

		winrt::com_ptr<IDxcBlob> argsBlob;
		ShaderUtils::CompileShader(argsBlob, L"data/shaders/GBufferArgs.hlsl", {}, L"cs_6_5", L"Main");
		m_ArgsShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, argsBlob->GetBufferPointer(), argsBlob->GetBufferSize());

		m_ArgsPipeline = device->createComputePipeline(
			nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ArgsShader)
			.addBindingLayout(m_ArgsBindingLayout));
	}

	void GBuffer::CheckGraphicsBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_GraphicsBindingSetDirty[currentSlot] && m_GraphicsBindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_RaytracingBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, sceneGraph->GetTransformBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(3, sceneGraph->GetPropertiesBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(4, sceneGraph->GetMeshSlotRemapBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(5, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::Texture_SRV(6, renderer->GetWaterDisplacementTexture()),
			nvrhi::BindingSetItem::Texture_SRV(7, scene->GetProjNoiseTexture()),
			nvrhi::BindingSetItem::Texture_SRV(8, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Sampler(1, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(2, m_PointWrapSampler)
		};

		m_GraphicsBindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_GraphicsBindingLayout);

		m_GraphicsBindingSetDirty[currentSlot] = false;
	}

	void GBuffer::CheckArgsBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_ArgsBindingSetDirty[currentSlot] && m_ArgsBindingSets[currentSlot])
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(uint32_t)),
			nvrhi::BindingSetItem::RawBuffer_SRV(0, sceneGraph->GetMeshSlotRemapBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_IndirectArgsBuffer)
		};

		m_ArgsBindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_ArgsBindingLayout);

		m_ArgsBindingSetDirty[currentSlot] = false;
	}

	void GBuffer::Execute(nvrhi::ICommandList* commandList)
	{
		CheckGraphicsBindings();
		CheckArgsBindings();

		auto* renderer = GetRenderer();

		uint32_t currentSlot = renderer->GetCurrentSlot();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto resolution = renderer->GetResolution();

		if (!m_FrameBuffer)
		{
			auto* gBufferOutput = renderer->GetGBufferOutput();

			auto frameBufferDesc = nvrhi::FramebufferDesc()
				.addColorAttachment(gBufferOutput->albedo)
				.addColorAttachment(gBufferOutput->normalRoughness)
				.addColorAttachment(gBufferOutput->emissiveMetallic)
				.setDepthAttachment(gBufferOutput->depth);

			m_FrameBuffer = renderer->GetDevice()->createFramebuffer(frameBufferDesc);
		}

		const auto& fbinfo = m_FrameBuffer->getFramebufferInfo();

		if (!m_GraphicsPipeline)
		{
			nvrhi::GraphicsPipelineDesc pipelineDesc;
			pipelineDesc.VS = m_VertexShader;
			pipelineDesc.PS = m_PixelShader;
			pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
			pipelineDesc.bindingLayouts = {
				m_GraphicsBindingLayout,
				sceneGraph->GetTriangleDescriptors()->m_Layout,
				sceneGraph->GetVertexDescriptors()->m_Layout,
				sceneGraph->GetMaterialDescriptors()->m_Layout,
				sceneGraph->GetTextureDescriptors()->m_Layout,
				sceneGraph->GetCubemapDescriptors()->m_Layout,
				sceneGraph->GetDynamicVertexDescriptors()->m_Layout
			};

			pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
			pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
			pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
			pipelineDesc.renderState.rasterState.setCullBack();

			m_GraphicsPipeline = GetRenderer()->GetDevice()->createGraphicsPipeline(pipelineDesc, fbinfo);
		}

		for (const auto& colorAttachment : m_FrameBuffer->getDesc().colorAttachments)
			commandList->clearTextureFloat(colorAttachment.texture, nvrhi::AllSubresources, nvrhi::Color(0.f));

		commandList->clearDepthStencilTexture(m_FrameBuffer->getDesc().depthAttachment.texture, nvrhi::AllSubresources, true, 1.f, true, 0);

		{
			auto& settings = Scene::GetSingleton()->m_Settings;

			m_RaytracingData->Roughness = settings.MaterialSettings.Roughness;
			m_RaytracingData->Metalness = settings.MaterialSettings.Metalness;

			m_RaytracingData->Emissive = settings.LightingSettings.Emissive;
			m_RaytracingData->Effect = settings.LightingSettings.Effect;
			m_RaytracingData->Sky = settings.LightingSettings.Sky;
			m_RaytracingData->WaterAbsorptionScale = settings.WaterSettings.AbsorptionScale;

			commandList->writeBuffer(m_RaytracingBuffer, m_RaytracingData.get(), sizeof(RaytracingData));
		}

		const uint32_t numMeshes = sceneGraph->GetNumMeshesFrame();

		if (numMeshes == 0)
			return;

		// Generate indirect draw arguments on the GPU: one DRAW_ARGUMENTS entry per mesh slot.
		{
			nvrhi::ComputeState argsState;
			argsState.pipeline = m_ArgsPipeline;
			argsState.bindings = { m_ArgsBindingSets[currentSlot] };
			commandList->setComputeState(argsState);

			commandList->setPushConstants(&numMeshes, sizeof(uint32_t));

			const uint32_t threadGroups = (numMeshes + 63u) / 64u;
			commandList->dispatch(threadGroups, 1, 1);
		}

		// Draw geometry
		{
			nvrhi::GraphicsState state;
			state.pipeline = m_GraphicsPipeline;
			state.framebuffer = m_FrameBuffer;
			state.bindings = {
				m_GraphicsBindingSets[currentSlot],
				sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
				sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
			};

			state.setIndirectParams(m_IndirectArgsBuffer);

			state.viewport.addViewportAndScissorRect(fbinfo.getViewport());

			commandList->setGraphicsState(state);

			commandList->drawIndirect(0, numMeshes);

			// Debug copy
			{
				auto* gBufferOutput = renderer->GetGBufferOutput();
				auto region = nvrhi::TextureSlice{ 0, 0, 0, resolution.x, resolution.y, 1 };
				commandList->copyTexture(GetRenderer()->GetMainTexture(), region, gBufferOutput->albedo, region);
			}
		}
	}
}
