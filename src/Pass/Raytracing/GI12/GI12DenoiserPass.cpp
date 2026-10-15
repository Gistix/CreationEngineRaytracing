#include "GI12DenoiserPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::GI12
{
	GI12DenoiserPass::GI12DenoiserPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_Resources(resources)
	{
		m_Defines = Util::Shader::GetGI12Defines(Scene::GetSingleton()->m_Settings);
	}

	void GI12DenoiserPass::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GI12DenoiserPass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSets.fill(nullptr);
		m_BindingSetDirty.fill(true);
	}

	void GI12DenoiserPass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetGI12Defines(settings);
		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_BindingSets.fill(nullptr);
			m_BindingSetDirty.fill(true);
		}
	}

	void GI12DenoiserPass::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			// Samplers s0-s3
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			nvrhi::BindingLayoutItem::Sampler(3),

			// CBVs b0-b8
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Raytracing
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // GI12CB
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3), // ScreenProbesCB
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(8), // GIDenoiserCB

			// SRVs t0-t14
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),  // Depth
			nvrhi::BindingLayoutItem::Texture_SRV(2),  // Albedo
			nvrhi::BindingLayoutItem::Texture_SRV(3),  // NormalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(4),  // VAOMAO
			nvrhi::BindingLayoutItem::Texture_SRV(5),  // MotionVectors
			nvrhi::BindingLayoutItem::Texture_SRV(6),  // FaceNormals
			nvrhi::BindingLayoutItem::Texture_SRV(7),  // SkyHemisphere
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8),  // Lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9),  // Instances
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10), // Meshes
			nvrhi::BindingLayoutItem::Texture_SRV(11), // PreviousColorBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(12), // PreviousColorDeltaBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(13), // PreviousDepthBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(14), // PreviousNormalBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(15), // ReflectionBuffer

			// UAVs u0-u4
			nvrhi::BindingLayoutItem::Texture_UAV(0), // BlurMask
			nvrhi::BindingLayoutItem::Texture_UAV(1), // ColorBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(2), // ColorDeltaBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(3), // IrradianceBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(4)  // MainOutput
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void GI12DenoiserPass::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		const wchar_t* shaderPath = L"data/shaders/raytracing/GI12/GI12Denoiser.hlsl";

		auto createKernel = [&](const wchar_t* entryPoint, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;

			winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(shaderPath, defines, ShaderStage::Compute, entryPoint);
			if (!blob) {
				logger::error("GI12DenoiserPass - Failed to compile kernel {}", Util::WStringToString(entryPoint));
				return;
			}

			shader = device->createShader({ nvrhi::ShaderType::Compute, "GI12Denoiser", Util::WStringToString(entryPoint).c_str() }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("GI12DenoiserPass - Failed to create shader {}", Util::WStringToString(entryPoint));
				return;
			}

			pipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout));
		};

		createKernel(L"ReprojectGI", m_ReprojectGIShader, m_ReprojectGIPipeline);
		createKernel(L"FilterGI", m_FilterGIShader, m_FilterGIPipeline);
		createKernel(L"CompositeGI", m_CompositeGIShader, m_CompositeGIPipeline);
	}

	void GI12DenoiserPass::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (m_BindingSets[currentSlot] && !m_BindingSetDirty[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto* renderTargets = renderer->GetRenderTargets();
		auto& textureManager = renderer->RenderTargetManager();

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_Resources->m_LinearWrapSampler),
			nvrhi::BindingSetItem::Sampler(1, m_Resources->m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(2, m_Resources->m_PointWrapSampler),
			nvrhi::BindingSetItem::Sampler(3, m_Resources->m_PointClampSampler),

			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, m_Resources->m_GI12CB),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_Resources->m_ScreenProbesCB),
			nvrhi::BindingSetItem::ConstantBuffer(8, m_Resources->m_GIDenoiserCB),

			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(3, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(4, renderTargets->gnmao),
			nvrhi::BindingSetItem::Texture_SRV(5, renderer->GetMotionVectorTexture()),
			nvrhi::BindingSetItem::Texture_SRV(6, textureManager.GetTexture(RenderTarget::FaceNormals)),
			nvrhi::BindingSetItem::Texture_SRV(7, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(8, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(9, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(10, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(11, m_Resources->m_PreviousColorBuffer),
			nvrhi::BindingSetItem::Texture_SRV(12, m_Resources->m_PreviousColorDeltaBuffer),
			nvrhi::BindingSetItem::Texture_SRV(13, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(14, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(15, m_Resources->m_ReflectionBuffer),

			nvrhi::BindingSetItem::Texture_UAV(0, m_Resources->m_BlurMask),
			nvrhi::BindingSetItem::Texture_UAV(1, m_Resources->m_ColorBuffer),
			nvrhi::BindingSetItem::Texture_UAV(2, m_Resources->m_ColorDeltaBuffer),
			nvrhi::BindingSetItem::Texture_UAV(3, m_Resources->m_IrradianceBuffer),
			nvrhi::BindingSetItem::Texture_UAV(4, renderer->GetMainTexture())
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GI12DenoiserPass::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto bindingSet = m_BindingSets[currentSlot];
		if (!bindingSet)
			return;

		const uint2 resolution = m_Resources->m_Resolution;

		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, uint32_t gx, uint32_t gy, uint32_t gz) {
			if (!pipeline) return;
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(gx, gy, gz);
		};

		// 1. Reproject GI
		dispatch(m_ReprojectGIPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);

		// 2. Separable Bilateral Spatial Filter
		// Horizontal pass (x = 1, y = 0)
		GIDenoiserConstants denoiserCB{};
		denoiserCB.blur_direction_x = 1;
		denoiserCB.blur_direction_y = 0;
		denoiserCB.pass_index = 0;
		commandList->writeBuffer(m_Resources->m_GIDenoiserCB, &denoiserCB, sizeof(denoiserCB));
		dispatch(m_FilterGIPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);

		// Vertical pass (x = 0, y = 1) -> writes final irradiance to m_IrradianceBuffer
		denoiserCB.blur_direction_x = 0;
		denoiserCB.blur_direction_y = 1;
		denoiserCB.pass_index = 1;
		commandList->writeBuffer(m_Resources->m_GIDenoiserCB, &denoiserCB, sizeof(denoiserCB));
		dispatch(m_FilterGIPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);

		// 3. Composite final shaded image: Albedo * Irradiance * (1/PI) + Specular -> MainTexture
		dispatch(m_CompositeGIPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);
	}
}
