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

		CreatePipeline();
	}

	void GBuffer::CreatePipeline()
	{
		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_RaytracingBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::PushConstants(3, sizeof(uint2)),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler)
		};

		nvrhi::utils::CreateBindingSetAndLayout(GetRenderer()->GetDevice(), nvrhi::ShaderType::All, 0, bindingSetDesc, m_BindingLayout, m_BindingSet);

		eastl::vector<DxcDefine> defines = { { L"RASTER", L"" } };

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> vertexBlob, pixelBlob;
		ShaderUtils::CompileShader(vertexBlob, L"data/shaders/GBufferRaster.hlsl", defines, L"vs_6_5", L"MainVS");
		ShaderUtils::CompileShader(pixelBlob, L"data/shaders/GBufferRaster.hlsl", defines, L"ps_6_5", L"MainPS");

		m_VertexShader = device->createShader({ nvrhi::ShaderType::Vertex, "", "MainVS" }, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize());
		m_PixelShader = device->createShader({ nvrhi::ShaderType::Pixel, "", "MainPS" }, pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize());
	}

	void GBuffer::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_FrameBuffer = nullptr;
		m_GraphicsPipeline = nullptr;
	}

	void GBuffer::Execute(nvrhi::ICommandList* commandList)
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto* renderer = GetRenderer();

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
				m_BindingLayout,
				sceneGraph->GetTriangleDescriptors()->m_Layout,
				sceneGraph->GetVertexDescriptors()->m_Layout,
				sceneGraph->GetTextureDescriptors()->m_Layout
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

			commandList->writeBuffer(m_RaytracingBuffer, m_RaytracingData.get(), sizeof(RaytracingData));
		}

		nvrhi::GraphicsState state;
		state.pipeline = m_GraphicsPipeline;
		state.framebuffer = m_FrameBuffer;
		state.bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable()
		};

		state.viewport.addViewportAndScissorRect(fbinfo.getViewport());

		commandList->setGraphicsState(state);

		const auto& instances = sceneGraph->GetInstances();

		for (uint i = 0; i < instances.size(); i++)
		{
			const auto& instance = instances[i];

			const auto& model = instance->model;

			for (uint m = 0; m < model->meshes.size(); m++)
			{
				auto constants = uint2(i, m);
				commandList->setPushConstants(&constants, sizeof(constants));

				nvrhi::DrawArguments args;
				args.vertexCount = model->meshes[m]->triangleCount * 3;
				args.instanceCount = 1;
				commandList->draw(args);
			}
		}
	}
}