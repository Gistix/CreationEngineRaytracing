#include "PTComposite.h"
#include "Renderer.h"
#include "Scene.h"

namespace
{
	eastl::vector<ShaderDefine> GetCompositeDefines(const Settings& settings)
	{
		eastl::vector<ShaderDefine> defines;

		if (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur) {
			defines.emplace_back(L"NRD", L"1");
			defines.emplace_back(L"NRD_REBLUR", L"1");
		} else if (settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax) {
			defines.emplace_back(L"NRD", L"1");
			defines.emplace_back(L"NRD_RELAX", L"1");
		}

		return defines;
	}
}

namespace Pass::Common
{
	PTComposite::PTComposite(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_Defines = GetCompositeDefines(Scene::GetSingleton()->m_Settings);
	}

	void PTComposite::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void PTComposite::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::Texture_SRV(3),
			nvrhi::BindingLayoutItem::Texture_SRV(4),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void PTComposite::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();

		auto defines = Util::Shader::GetDXCDefines(m_Defines);

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/PTComposite.hlsl", defines, ShaderStage::Compute);

		if (!shaderBlob)
			return;

		auto computeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!computeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(computeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void PTComposite::SettingsChanged(const Settings& settings)
	{
		auto defines = GetCompositeDefines(settings);

		if (defines != m_Defines) {
			m_Defines = eastl::move(defines);
			CreatePipeline();
		}

		m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
					 settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);
	}

	void PTComposite::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();

		auto* renderer = GetRenderer();

		auto& textureManager = renderer->RenderTargetManager();

		auto* diffuseAlbedo = textureManager.GetTexture(RenderTarget::DiffuseAlbedo);

		auto* diffuseRadiance = textureManager.GetTexture(RenderTarget::DiffuseRadiance);
		auto* specularRadiance = textureManager.GetTexture(RenderTarget::SpecularRadiance);

		auto* diffuseFactor = textureManager.GetTexture(RenderTarget::DiffuseFactor);
		auto* specularFactor = textureManager.GetTexture(RenderTarget::SpecularFactor);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, scene->GetFeatureBuffer()),			
			nvrhi::BindingSetItem::Texture_SRV(0, diffuseAlbedo),
			nvrhi::BindingSetItem::Texture_SRV(1, diffuseRadiance),
			nvrhi::BindingSetItem::Texture_SRV(2, specularRadiance),
			nvrhi::BindingSetItem::Texture_SRV(3, diffuseFactor),
			nvrhi::BindingSetItem::Texture_SRV(4, specularFactor),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_BindingSetDirty[currentSlot] = false;
	}

	void PTComposite::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		auto resolution = Renderer::GetSingleton()->GetResolution();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}