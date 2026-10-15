#include "GI12ScreenProbesPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::GI12
{
	GI12ScreenProbesPass::GI12ScreenProbesPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_Resources(resources)
	{
		m_Defines = Util::Shader::GetGI12Defines(Scene::GetSingleton()->m_Settings);
		if (m_SceneTLAS) {
			m_SceneTLAS->GetTopLevelAS().AddListener(this);
		}
	}

	void GI12ScreenProbesPass::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GI12ScreenProbesPass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSets.fill(nullptr);
		m_BindingSetDirty.fill(true);
	}

	void GI12ScreenProbesPass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetGI12Defines(settings);
		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_BindingSets.fill(nullptr);
			m_BindingSetDirty.fill(true);
		}
	}

	void GI12ScreenProbesPass::CreateBindingLayout()
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
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3), // ScreenProbesCB
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(6), // GlossyReflectionsCB

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
			nvrhi::BindingLayoutItem::Texture_SRV(11), // PreviousProbeMaskBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(12), // PreviousProbeBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13), // PreviousProbeSHBuffer

			// UAVs u0-u12
			nvrhi::BindingLayoutItem::Texture_UAV(0),           // ProbeMaskBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(1),           // ProbeBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),  // ProbeSHBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),  // ProbeSpawnBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4),  // ProbeSpawnSampleBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5),  // ProbeSpawnRadianceBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),  // EmptyTileBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7),  // EmptyTileCountBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(8),  // ProbeCachedTileLRUBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(9),           // IrradianceBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(10),          // GIDenoiser_ColorBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(11), // GlossyReflections_RtSampleBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(12)  // GlossyReflections_RtSampleCountBuffer
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void GI12ScreenProbesPass::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		const wchar_t* shaderPath = L"data/shaders/raytracing/GI12/GI12ScreenProbes.hlsl";

		auto createKernel = [&](const wchar_t* entryPoint, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;

			winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(shaderPath, defines, ShaderStage::Compute, entryPoint);
			if (!blob) {
				logger::error("GI12ScreenProbesPass - Failed to compile kernel {}", Util::WStringToString(entryPoint));
				return;
			}

			shader = device->createShader({ nvrhi::ShaderType::Compute, "GI12ScreenProbes", Util::WStringToString(entryPoint).c_str() }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("GI12ScreenProbesPass - Failed to create shader {}", Util::WStringToString(entryPoint));
				return;
			}

			pipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout));
		};

		createKernel(L"ClearProbeMask", m_ClearProbeMaskShader, m_ClearProbeMaskPipeline);
		createKernel(L"InitCachedTileLRU", m_InitCachedTileLRUShader, m_InitCachedTileLRUPipeline);
		createKernel(L"ReprojectScreenProbes", m_ReprojectScreenProbesShader, m_ReprojectScreenProbesPipeline);
		createKernel(L"CountAndScatterProbes", m_CountAndScatterProbesShader, m_CountAndScatterProbesPipeline);
		createKernel(L"SpawnScreenProbes", m_SpawnScreenProbesShader, m_SpawnScreenProbesPipeline);
		createKernel(L"SampleScreenProbes", m_SampleScreenProbesShader, m_SampleScreenProbesPipeline);
		createKernel(L"BlendScreenProbes", m_BlendScreenProbesShader, m_BlendScreenProbesPipeline);
		createKernel(L"ProjectScreenProbes", m_ProjectScreenProbesShader, m_ProjectScreenProbesPipeline);
		createKernel(L"InterpolateScreenProbes", m_InterpolateScreenProbesShader, m_InterpolateScreenProbesPipeline);
	}

	void GI12ScreenProbesPass::CheckBindings()
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
			nvrhi::BindingSetItem::ConstantBuffer(6, m_Resources->m_GlossyReflectionsCB),

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
			nvrhi::BindingSetItem::Texture_SRV(11, m_Resources->m_PreviousProbeMaskBuffer),
			nvrhi::BindingSetItem::Texture_SRV(12, m_Resources->m_PreviousProbeBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(13, m_Resources->m_PreviousProbeSHBuffer),

			nvrhi::BindingSetItem::Texture_UAV(0, m_Resources->m_ProbeMaskBuffer),
			nvrhi::BindingSetItem::Texture_UAV(1, m_Resources->m_ProbeBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_Resources->m_ProbeSHBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(3, m_Resources->m_ProbeSpawnBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(4, m_Resources->m_ProbeSpawnSampleBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(5, m_Resources->m_ProbeSpawnRadianceBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(6, m_Resources->m_EmptyTileBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(7, m_Resources->m_EmptyTileCountBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(8, m_Resources->m_ProbeCachedTileLRUBuffer),
			nvrhi::BindingSetItem::Texture_UAV(9, m_Resources->m_IrradianceBuffer),
			nvrhi::BindingSetItem::Texture_UAV(10, m_Resources->m_ColorBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(11, m_Resources->m_RtSampleBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(12, m_Resources->m_RtSampleCountBuffer)
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GI12ScreenProbesPass::ExecuteGenerate(nvrhi::ICommandList* commandList)
	{
		CheckBindings();
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto bindingSet = m_BindingSets[currentSlot];
		if (!bindingSet)
			return;

		const uint2 probeCount = m_Resources->m_ProbeCount;
		const uint32_t totalTiles = probeCount.x * probeCount.y;

		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, uint32_t gx, uint32_t gy, uint32_t gz) {
			if (!pipeline) return;
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(gx, gy, gz);
		};

		// 1. Clear Probe Mask
		dispatch(m_ClearProbeMaskPipeline, (probeCount.x + 7) / 8, (probeCount.y + 7) / 8, 1);

		// 2. Init Cached Tile LRU
		dispatch(m_InitCachedTileLRUPipeline, (totalTiles + 63) / 64, 1, 1);

		// 3. Reproject Screen Probes
		dispatch(m_ReprojectScreenProbesPipeline, (probeCount.x + 7) / 8, (probeCount.y + 7) / 8, 1);

		// 4. Count and Scatter Probes (2D kernel: 8x8 threads per group)
		dispatch(m_CountAndScatterProbesPipeline, (probeCount.x + 7) / 8, (probeCount.y + 7) / 8, 1);

		// 5. Spawn Screen Probes (1 thread per probe)
		dispatch(m_SpawnScreenProbesPipeline, (totalTiles + 63) / 64, 1, 1);

		// 6. Sample Screen Probes (64 rays per probe -> totalTiles * 64 threads -> totalTiles groups)
		dispatch(m_SampleScreenProbesPipeline, totalTiles, 1, 1);
	}

	void GI12ScreenProbesPass::ExecuteInterpolate(nvrhi::ICommandList* commandList)
	{
		CheckBindings();
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto bindingSet = m_BindingSets[currentSlot];
		if (!bindingSet)
			return;

		const uint2 probeCount = m_Resources->m_ProbeCount;
		const uint32_t totalTiles = probeCount.x * probeCount.y;
		const uint2 resolution = m_Resources->m_Resolution;

		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, uint32_t gx, uint32_t gy, uint32_t gz) {
			if (!pipeline) return;
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(gx, gy, gz);
		};

		// 7. Blend Screen Probes (64 rays per probe -> totalTiles * 64 threads -> totalTiles groups)
		dispatch(m_BlendScreenProbesPipeline, totalTiles, 1, 1);

		// 8. Project Screen Probes to SH (1 thread per probe)
		dispatch(m_ProjectScreenProbesPipeline, (totalTiles + 63) / 64, 1, 1);

		// 9. Full Resolution Bilateral Interpolation (outputs IrradianceBuffer & queues Glossy samples)
		dispatch(m_InterpolateScreenProbesPipeline, (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1);

		auto denoiser = Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser;
		if (denoiser == Denoiser::NRD_Reblur || denoiser == Denoiser::NRD_Relax) {
			auto* renderer = GetRenderer();
			auto fullRes = renderer->GetResolution();
			auto copyRegion = nvrhi::TextureSlice{ 0, 0, 0, fullRes.x, fullRes.y, 1 };
			auto* diffuseRadiance = renderer->RenderTargetManager().GetTexture(RenderTarget::DiffuseRadiance);
			if (diffuseRadiance && m_Resources->m_IrradianceBuffer) {
				commandList->copyTexture(diffuseRadiance, copyRegion, m_Resources->m_IrradianceBuffer, copyRegion);
			}
		}
	}
}
