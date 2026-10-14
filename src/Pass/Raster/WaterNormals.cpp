#include "WaterNormals.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Raster
{
	WaterNormals::WaterNormals(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
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

		m_IndirectArgsBuffer = RingBuffer(renderer->GetDevice(), nvrhi::BufferDesc()
			.setByteSize(Constants::NUM_MESHES_MAX * 16)
			.setStructStride(16)
			.setCanHaveUAVs(true)
			.setIsDrawIndirectArgs(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::IndirectArgument),
			"WaterNormals Indirect Args");
	}

	void WaterNormals::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_FrameBuffer = nullptr;
		m_GraphicsPipeline = nullptr;
		m_DepthTexture = nullptr;
	}

	void WaterNormals::Initialize()
	{
		CreatePipeline();
	}

	void WaterNormals::CreatePipeline()
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
			nvrhi::BindingLayoutItem::RawBuffer_SRV(2),        // Materials (type cull)
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)  // IndirectArgs
		};

		m_ArgsBindingLayout = device->createBindingLayout(argsLayoutDesc);

		eastl::vector<DxcDefine> defines = { { L"RASTER", L"" } };

		winrt::com_ptr<IDxcBlob> vertexBlob, pixelBlob;
		ShaderUtils::CompileShader(vertexBlob, L"data/shaders/WaterNormalsRaster.hlsl", defines, ShaderStage::Vertex, L"MainVS");
		ShaderUtils::CompileShader(pixelBlob, L"data/shaders/WaterNormalsRaster.hlsl", defines, ShaderStage::Pixel, L"MainPS");

		if (!vertexBlob || !pixelBlob)
			return;

		m_VertexShader = device->createShader({ nvrhi::ShaderType::Vertex, "", "MainVS" }, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize());
		m_PixelShader = device->createShader({ nvrhi::ShaderType::Pixel, "", "MainPS" }, pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize());

		winrt::com_ptr<IDxcBlob> argsBlob;
		ShaderUtils::CompileShader(argsBlob, L"data/shaders/WaterNormalsArgs.hlsl", {}, ShaderStage::Compute, L"Main");
		m_ArgsShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, argsBlob->GetBufferPointer(), argsBlob->GetBufferSize());

		m_ArgsPipeline = device->createComputePipeline(
			nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ArgsShader)
			.addBindingLayout(m_ArgsBindingLayout));
	}

	void WaterNormals::CheckGraphicsBindings()
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
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
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

	void WaterNormals::CheckArgsBindings()
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
			nvrhi::BindingSetItem::RawBuffer_SRV(2, sceneGraph->GetMaterialBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_IndirectArgsBuffer.current())
		};

		m_ArgsBindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_ArgsBindingLayout);

		m_ArgsBindingSetDirty[currentSlot] = false;
	}

	void WaterNormals::Execute(nvrhi::ICommandList* commandList)
	{
		CheckGraphicsBindings();
		CheckArgsBindings();

		auto* renderer = GetRenderer();

		uint32_t currentSlot = renderer->GetCurrentSlot();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto& textureManager = renderer->RenderTargetManager();

		if (!m_DepthTexture)
		{
			auto device = renderer->GetDevice();
			const auto resolution = renderer->GetResolution();

			const nvrhi::Format depthFormats[] = {
				nvrhi::Format::D24S8,
				nvrhi::Format::D32S8,
				nvrhi::Format::D32,
				nvrhi::Format::D16 };

			const nvrhi::FormatSupport depthFeatures =
				nvrhi::FormatSupport::Texture |
				nvrhi::FormatSupport::DepthStencil |
				nvrhi::FormatSupport::ShaderLoad;

			nvrhi::TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.format = nvrhi::utils::ChooseFormat(device, depthFeatures, depthFormats, std::size(depthFormats));
			desc.isRenderTarget = true;
			desc.isTypeless = true;
			desc.initialState = nvrhi::ResourceStates::DepthWrite;
			desc.useClearValue = true;
			desc.clearValue = nvrhi::Color(1.f, 0.f, 0.f, 0.f);
			desc.keepInitialState = true;
			desc.debugName = "WaterNormals Depth Texture";
			m_DepthTexture = device->createTexture(desc);
		}

		if (!m_FrameBuffer)
		{
			auto frameBufferDesc = nvrhi::FramebufferDesc()
				.addColorAttachment(textureManager.GetTexture(RenderTarget::WaterNormal))
				.addColorAttachment(textureManager.GetTexture(RenderTarget::WaterDepth))
				.setDepthAttachment(m_DepthTexture);

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
				sceneGraph->GetPrevPositionDescriptors()->m_Layout,
				sceneGraph->GetCubemapDescriptors()->m_Layout,
				sceneGraph->GetDynamicVertexDescriptors()->m_Layout
			};

			pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
			pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
			pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
			pipelineDesc.renderState.rasterState.setCullBack();

			m_GraphicsPipeline = GetRenderer()->GetDevice()->createGraphicsPipeline(pipelineDesc, fbinfo);
		}

		commandList->clearTextureFloat(textureManager.GetTexture(RenderTarget::WaterNormal), nvrhi::AllSubresources, nvrhi::Color(0.f));
		commandList->clearTextureFloat(textureManager.GetTexture(RenderTarget::WaterDepth), nvrhi::AllSubresources, nvrhi::Color(1.f, 1.f, 1.f, 1.f));

		commandList->clearDepthStencilTexture(m_DepthTexture, nvrhi::AllSubresources, true, 1.f, true, 0);

		const uint32_t numMeshes = sceneGraph->GetNumMeshesFrame();

		if (numMeshes == 0)
			return;

		// Generate indirect draw arguments on the GPU: one DRAW_ARGUMENTS entry per mesh slot,
		// culled to water meshes by the args shader.
		{
			nvrhi::ComputeState argsState;
			argsState.pipeline = m_ArgsPipeline;
			argsState.bindings = { m_ArgsBindingSets[currentSlot] };
			commandList->setComputeState(argsState);

			commandList->setPushConstants(&numMeshes, sizeof(uint32_t));

			const uint32_t threadGroups = (numMeshes + 63u) / 64u;
			commandList->dispatch(threadGroups, 1, 1);
		}

		// Draw water geometry
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
				sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
				sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
			};

			state.setIndirectParams(m_IndirectArgsBuffer.current());

			auto viewport = fbinfo.getViewport();

			const auto dynamicResolution = Renderer::GetSingleton()->GetDynamicResolution();
			viewport.maxX = static_cast<float>(dynamicResolution.x);
			viewport.maxY = static_cast<float>(dynamicResolution.y);

			state.viewport.addViewportAndScissorRect(viewport);

			commandList->setGraphicsState(state);

			commandList->drawIndirect(0, numMeshes);
		}
	}
}
