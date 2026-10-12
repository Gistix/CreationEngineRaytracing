#include "DDGIDebugPass.h"

#include "DDGIPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderUtils.h"
#include "Util.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::Common
{
	DDGIDebugPass::DDGIDebugPass(Renderer* renderer, DDGIPass* ddgiPass)
		: RenderPass(renderer), m_DDGIPass(ddgiPass)
	{
	}

	void DDGIDebugPass::Initialize()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::Compute;
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::Texture_SRV(3),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};
		m_BindingLayout = device->createBindingLayout(layoutDesc);

		auto defines = Util::Shader::GetDDGIDefines(Scene::GetSingleton()->m_Settings);
		auto dxcDefines = Util::Shader::GetDXCDefines(defines);
		winrt::com_ptr<IDxcBlob> blob;
		ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/DDGI/DDGIDebug.hlsl", dxcDefines, ShaderStage::Compute);
		if (!blob)
		{
			logger::error("DDGIDebugPass::Initialize - Failed to compile DDGIDebug.hlsl.");
			return;
		}

		auto shader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
		if (shader)
			m_ComputePipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(m_BindingLayout));

		if (!m_ComputePipeline)
			logger::error("DDGIDebugPass::Initialize - Failed to create compute pipeline.");

		CreateProbeGridTarget(GetRenderer()->GetResolution());

		nvrhi::BindingLayoutDesc gridLayoutDesc;
		gridLayoutDesc.visibility = nvrhi::ShaderType::AllGraphics;
		gridLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::Texture_SRV(0)
		};
		m_ProbeGridBindingLayout = device->createBindingLayout(gridLayoutDesc);

		winrt::com_ptr<IDxcBlob> vertexBlob;
		winrt::com_ptr<IDxcBlob> pixelBlob;
		ShaderUtils::CompileShader(vertexBlob, L"data/shaders/raytracing/DDGI/DDGIProbeGrid.hlsl", dxcDefines, ShaderStage::Vertex, L"VSMain");
		ShaderUtils::CompileShader(pixelBlob, L"data/shaders/raytracing/DDGI/DDGIProbeGrid.hlsl", dxcDefines, ShaderStage::Pixel, L"PSMain");
		if (vertexBlob && pixelBlob && m_ProbeGridFramebuffer)
		{
			auto vertexShader = device->createShader({ nvrhi::ShaderType::Vertex, "", "VSMain" }, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize());
			auto pixelShader = device->createShader({ nvrhi::ShaderType::Pixel, "", "PSMain" }, pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize());
			nvrhi::GraphicsPipelineDesc pipelineDesc;
			pipelineDesc.VS = vertexShader;
			pipelineDesc.PS = pixelShader;
			pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
			pipelineDesc.bindingLayouts = { m_ProbeGridBindingLayout };
			pipelineDesc.renderState.rasterState.setCullNone();
			pipelineDesc.renderState.depthStencilState.disableDepthTest();
			pipelineDesc.renderState.depthStencilState.disableDepthWrite();
			m_ProbeGridPipeline = device->createGraphicsPipeline(pipelineDesc, m_ProbeGridFramebuffer->getFramebufferInfo());
		}
		if (!m_ProbeGridPipeline)
			logger::error("DDGIDebugPass::Initialize - Failed to create DDGI probe grid pipeline.");

		m_Initialized = static_cast<bool>(m_ComputePipeline);
	}

	void DDGIDebugPass::SettingsChanged(const Settings& settings)
	{
		m_Enabled = settings.DDGI.Enabled &&
			settings.DDGI.DebugView != DDGIDebugView::None;
		logger::info("[DDGI Debug] settings: enabled={}, view={}", settings.DDGI.Enabled,
			static_cast<uint32_t>(settings.DDGI.DebugView));
		m_BindingSetDirty.fill(true);
		m_ProbeGridBindingSetDirty.fill(true);
	}

	void DDGIDebugPass::CreateProbeGridTarget(uint2 resolution)
	{
		auto device = GetRenderer()->GetDevice();
		nvrhi::TextureDesc textureDesc;
		textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
		textureDesc.width = resolution.x;
		textureDesc.height = resolution.y;
		textureDesc.format = nvrhi::Format::RGBA16_FLOAT;
		textureDesc.isRenderTarget = true;
		textureDesc.isUAV = true;
		textureDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		textureDesc.keepInitialState = true;
		textureDesc.debugName = "DDGI Probe Grid";
		m_ProbeGridTexture = device->createTexture(textureDesc);
		m_ProbeGridFramebuffer = device->createFramebuffer(
			nvrhi::FramebufferDesc().addColorAttachment(m_ProbeGridTexture));
	}

	void DDGIDebugPass::ResolutionChanged(uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
		m_ProbeGridBindingSetDirty.fill(true);
		CreateProbeGridTarget(resolution);
	}

	void DDGIDebugPass::CheckBindings()
	{
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();
		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_DDGIPass->GetVolumeConstantBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, m_DDGIPass->GetProbeIrradiance()),
			nvrhi::BindingSetItem::Texture_SRV(1, m_DDGIPass->GetProbeDistance()),
			nvrhi::BindingSetItem::Texture_SRV(2, m_DDGIPass->GetProbeData()),
			nvrhi::BindingSetItem::Texture_SRV(3, m_DDGIPass->GetRayData()),
			nvrhi::BindingSetItem::Texture_UAV(0, GetRenderer()->GetMainTexture())
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void DDGIDebugPass::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;
		if (Scene::GetSingleton()->m_Settings.DDGI.DebugView == DDGIDebugView::ProbeGrid)
		{
			if (!m_LoggedProbeGridExecution)
			{
				logger::info("[DDGI Debug] ProbeGrid executing: pipeline={}, framebuffer={}, probes={}",
					static_cast<bool>(m_ProbeGridPipeline), static_cast<bool>(m_ProbeGridFramebuffer),
					m_DDGIPass->GetProbeCount());
				m_LoggedProbeGridExecution = true;
			}
			ExecuteProbeGrid(commandList);
			return;
		}
		if (!m_ComputePipeline)
			return;

		CheckBindings();
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSets[currentSlot])
			return;

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		const auto resolution = GetRenderer()->GetResolution();
		commandList->dispatch(
			Util::Math::DivideRoundUp(resolution.x, 8u),
			Util::Math::DivideRoundUp(resolution.y, 8u));
	}

	void DDGIDebugPass::CheckProbeGridBindings()
	{
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_ProbeGridBindingSetDirty[currentSlot] && m_ProbeGridBindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();
		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_DDGIPass->GetVolumeConstantBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, m_DDGIPass->GetProbeData())
		};
		m_ProbeGridBindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(desc, m_ProbeGridBindingLayout);
		m_ProbeGridBindingSetDirty[currentSlot] = false;
	}

	void DDGIDebugPass::ExecuteProbeGrid(nvrhi::ICommandList* commandList)
	{
		if (!m_ProbeGridPipeline || !m_ProbeGridFramebuffer)
			return;

		CheckProbeGridBindings();
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_ProbeGridBindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();
		const auto resolution = renderer->GetResolution();
		const auto copyRegion = nvrhi::TextureSlice{ 0, 0, 0, resolution.x, resolution.y, 1 };
		// ProbeGrid is a standalone spatial view. Don't seed it from the GI output:
		// GI can be black while probes are still valid, which hides whether this
		// visualization pass is drawing at all.
		commandList->clearTextureFloat(m_ProbeGridTexture, nvrhi::AllSubresources, nvrhi::Color(0.025f, 0.035f, 0.05f, 1.0f));

		nvrhi::GraphicsState state;
		state.pipeline = m_ProbeGridPipeline;
		state.framebuffer = m_ProbeGridFramebuffer;
		state.bindings = { m_ProbeGridBindingSets[currentSlot] };
		auto viewport = m_ProbeGridFramebuffer->getFramebufferInfo().getViewport();
		viewport.maxX = static_cast<float>(resolution.x);
		viewport.maxY = static_cast<float>(resolution.y);
		state.viewport.addViewportAndScissorRect(viewport);
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(m_DDGIPass->GetProbeCount()));

		commandList->copyTexture(renderer->GetMainTexture(), copyRegion, m_ProbeGridTexture, copyRegion);
	}
}
