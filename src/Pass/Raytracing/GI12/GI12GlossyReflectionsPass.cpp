#include "GI12GlossyReflectionsPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::GI12
{
	GI12GlossyReflectionsPass::GI12GlossyReflectionsPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_Resources(resources)
	{
		m_Defines = Util::Shader::GetGI12Defines(Scene::GetSingleton()->m_Settings);
		if (m_SceneTLAS) {
			m_SceneTLAS->GetTopLevelAS().AddListener(this);
		}
	}

	void GI12GlossyReflectionsPass::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GI12GlossyReflectionsPass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSets.fill(nullptr);
		m_BindingSetDirty.fill(true);
	}

	void GI12GlossyReflectionsPass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetGI12Defines(settings);
		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_BindingSets.fill(nullptr);
			m_BindingSetDirty.fill(true);
		}
	}

	void GI12GlossyReflectionsPass::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			// Samplers s0-s3
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			nvrhi::BindingLayoutItem::Sampler(3),

			// CBVs
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Raytracing
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // GI12CB
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(6), // GlossyReflectionsCB
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(7), // GlossyAtrousCB

			// SRVs t0-t13
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
			nvrhi::BindingLayoutItem::Texture_SRV(11), // PreviousReflectionBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(12), // PreviousDepthBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(13), // PreviousNormalBuffer

			// UAVs u0-u10
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // RtSampleBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1), // RtSampleCountBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(2),          // SpecularBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(3),          // DirectionBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(4),          // ReflectionsBuffer0
			nvrhi::BindingLayoutItem::Texture_UAV(5),          // AverageSquaredBuffer0
			nvrhi::BindingLayoutItem::Texture_UAV(6),          // ReflectionsBuffer1
			nvrhi::BindingLayoutItem::Texture_UAV(7),          // AverageSquaredBuffer1
			nvrhi::BindingLayoutItem::Texture_UAV(8),          // FirefliesBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(9),          // StandardDevBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(10)          // ReflectionBuffer
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void GI12GlossyReflectionsPass::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		const wchar_t* shaderPath = L"data/shaders/raytracing/GI12/GI12GlossyReflections.hlsl";

		auto createKernel = [&](const wchar_t* entryPoint, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;

			winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(shaderPath, defines, ShaderStage::Compute, entryPoint);
			if (!blob) {
				logger::error("GI12GlossyReflectionsPass - Failed to compile kernel {}", Util::WStringToString(entryPoint));
				return;
			}

			shader = device->createShader({ nvrhi::ShaderType::Compute, "GI12GlossyReflections", Util::WStringToString(entryPoint).c_str() }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("GI12GlossyReflectionsPass - Failed to create shader {}", Util::WStringToString(entryPoint));
				return;
			}

			pipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout));
		};

		createKernel(L"ClearCounters", m_ClearCountersShader, m_ClearCountersPipeline);
		createKernel(L"TraceReflections", m_TraceReflectionsShader, m_TraceReflectionsPipeline);
		createKernel(L"ResolveReflections_Atrous_First", m_ResolveReflectionsAtrousFirstShader, m_ResolveReflectionsAtrousFirstPipeline);
		createKernel(L"ResolveReflections_Atrous_Iter", m_ResolveReflectionsAtrousIterShader, m_ResolveReflectionsAtrousIterPipeline);
		createKernel(L"ResolveReflections_Atrous_Last", m_ResolveReflectionsAtrousLastShader, m_ResolveReflectionsAtrousLastPipeline);
		createKernel(L"MarkFireflies", m_MarkFirefliesShader, m_MarkFirefliesPipeline);
		createKernel(L"CleanupFireflies", m_CleanupFirefliesShader, m_CleanupFirefliesPipeline);
		createKernel(L"ReprojectReflections", m_ReprojectReflectionsShader, m_ReprojectReflectionsPipeline);
	}

	void GI12GlossyReflectionsPass::CheckBindings()
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
			nvrhi::BindingSetItem::ConstantBuffer(6, m_Resources->m_GlossyReflectionsCB),
			nvrhi::BindingSetItem::ConstantBuffer(7, m_Resources->m_GlossyAtrousCB),

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
			nvrhi::BindingSetItem::Texture_SRV(11, m_Resources->m_PreviousReflectionBuffer),
			nvrhi::BindingSetItem::Texture_SRV(12, renderer->GetDepthTexture()), // Previous depth
			nvrhi::BindingSetItem::Texture_SRV(13, renderTargets->normalRoughness), // Previous normal

			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_Resources->m_RtSampleBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_Resources->m_RtSampleCountBuffer),
			nvrhi::BindingSetItem::Texture_UAV(2, m_Resources->m_SpecularBuffer),
			nvrhi::BindingSetItem::Texture_UAV(3, m_Resources->m_DirectionBuffer),
			nvrhi::BindingSetItem::Texture_UAV(4, m_Resources->m_ReflectionsBuffer0),
			nvrhi::BindingSetItem::Texture_UAV(5, m_Resources->m_AverageSquaredBuffer0),
			nvrhi::BindingSetItem::Texture_UAV(6, m_Resources->m_ReflectionsBuffer1),
			nvrhi::BindingSetItem::Texture_UAV(7, m_Resources->m_AverageSquaredBuffer1),
			nvrhi::BindingSetItem::Texture_UAV(8, m_Resources->m_FirefliesBuffer),
			nvrhi::BindingSetItem::Texture_UAV(9, m_Resources->m_StandardDevBuffer),
			nvrhi::BindingSetItem::Texture_UAV(10, m_Resources->m_ReflectionBuffer)
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GI12GlossyReflectionsPass::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto bindingSet = m_BindingSets[currentSlot];
		if (!bindingSet)
			return;

		const uint2 resolution = m_Resources->m_Resolution;
		const uint2 halfRes = uint2((resolution.x + 1) / 2, (resolution.y + 1) / 2);

		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, uint32_t gx, uint32_t gy, uint32_t gz) {
			if (!pipeline) return;
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(gx, gy, gz);
		};

		// 1. Trace Reflections
		dispatch(m_TraceReflectionsPipeline, (resolution.x * resolution.y + 63) / 64, 1, 1);

		// 2. Mark and Cleanup Fireflies
		dispatch(m_MarkFirefliesPipeline, (halfRes.x + 7) / 8, (halfRes.y + 7) / 8, 1);
		dispatch(m_CleanupFirefliesPipeline, (halfRes.x + 7) / 8, (halfRes.y + 7) / 8, 1);

		// 3. Multi-scale A-trous Wavelet Filter
		// First pass
		GlossyReflectionsAtrousConstants atrousCB{};
		atrousCB.full_step = 1;
		atrousCB.ping_pong = 0;
		commandList->writeBuffer(m_Resources->m_GlossyAtrousCB, &atrousCB, sizeof(atrousCB));
		dispatch(m_ResolveReflectionsAtrousFirstPipeline, (halfRes.x + 7) / 8, (halfRes.y + 7) / 8, 1);

		// Iterative passes (step 2, 4)
		for (int step = 2; step <= 4; step *= 2) {
			atrousCB.full_step = step;
			atrousCB.ping_pong = (atrousCB.ping_pong == 0) ? 1 : 0;
			commandList->writeBuffer(m_Resources->m_GlossyAtrousCB, &atrousCB, sizeof(atrousCB));
			dispatch(m_ResolveReflectionsAtrousIterPipeline, (halfRes.x + 7) / 8, (halfRes.y + 7) / 8, 1);
		}

		// Final pass to full resolution
		atrousCB.full_step = 8;
		commandList->writeBuffer(m_Resources->m_GlossyAtrousCB, &atrousCB, sizeof(atrousCB));
		dispatch(m_ResolveReflectionsAtrousLastPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);

		// 4. Reproject Reflections
		dispatch(m_ReprojectReflectionsPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);

		// 5. Clear Counters for next frame
		dispatch(m_ClearCountersPipeline, 1, 1, 1);

		auto denoiser = Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser;
		if (denoiser == Denoiser::NRD_Reblur || denoiser == Denoiser::NRD_Relax) {
			auto* renderer = GetRenderer();
			auto fullRes = renderer->GetResolution();
			auto copyRegion = nvrhi::TextureSlice{ 0, 0, 0, fullRes.x, fullRes.y, 1 };
			auto* specularRadiance = renderer->RenderTargetManager().GetTexture(RenderTarget::SpecularRadiance);
			if (specularRadiance && m_Resources->m_ReflectionBuffer) {
				commandList->copyTexture(specularRadiance, copyRegion, m_Resources->m_ReflectionBuffer, copyRegion);
			}
		}
	}
}
