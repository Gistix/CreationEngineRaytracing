#include "GI12HashGridCachePass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::GI12
{
	GI12HashGridCachePass::GI12HashGridCachePass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_Resources(resources)
	{
		m_Defines = Util::Shader::GetGI12Defines(Scene::GetSingleton()->m_Settings);
		if (m_SceneTLAS) {
			m_SceneTLAS->GetTopLevelAS().AddListener(this);
		}
	}

	void GI12HashGridCachePass::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GI12HashGridCachePass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSets.fill(nullptr);
		m_BindingSetDirty.fill(true);
	}

	void GI12HashGridCachePass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetGI12Defines(settings);
		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_BindingSets.fill(nullptr);
			m_BindingSetDirty.fill(true);
		}
	}

	void GI12HashGridCachePass::CreateBindingLayout()
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
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(4), // HashGridCacheCB

			// SRVs t0-t10
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

			// UAVs u0-u14
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),  // HashBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),  // DecayTileBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),  // ValueBuffer (CellBuffer)
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),  // ValueIndirectBuffer (MultibounceCellBuffer)
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4),  // UpdateTileBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5),  // UpdateTileCountBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),  // VisibilityBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7),  // VisibilityCountBuffer0
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(8),  // VisibilityCountBuffer1
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(9),  // VisibilityCellBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(10), // MultibounceInfoBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(11), // ProbeSpawnRadianceBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(12), // ProbeSpawnSampleBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(13), // ProbeSpawnBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(14)  // EmptyTileCountBuffer
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void GI12HashGridCachePass::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		const wchar_t* shaderPath = L"data/shaders/raytracing/GI12/GI12HashGridCache.hlsl";

		auto createKernel = [&](const wchar_t* entryPoint, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;

			winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(shaderPath, defines, ShaderStage::Compute, entryPoint);
			if (!blob) {
				logger::error("GI12HashGridCachePass - Failed to compile kernel {}", Util::WStringToString(entryPoint));
				return;
			}

			shader = device->createShader({ nvrhi::ShaderType::Compute, "GI12HashGridCache", Util::WStringToString(entryPoint).c_str() }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("GI12HashGridCachePass - Failed to create shader {}", Util::WStringToString(entryPoint));
				return;
			}

			pipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout));
		};

		createKernel(L"PurgeTiles", m_PurgeTilesShader, m_PurgeTilesPipeline);
		createKernel(L"PopulateScreenProbesCells", m_PopulateScreenProbesCellsShader, m_PopulateScreenProbesCellsPipeline);
		createKernel(L"PopulateMultibounceCells", m_PopulateMultibounceCellsShader, m_PopulateMultibounceCellsPipeline);
		createKernel(L"UpdateTiles", m_UpdateTilesShader, m_UpdateTilesPipeline);
		createKernel(L"UpdateMultibounceCells", m_UpdateMultibounceCellsShader, m_UpdateMultibounceCellsPipeline);
		createKernel(L"ResolveCells", m_ResolveCellsShader, m_ResolveCellsPipeline);
	}

	void GI12HashGridCachePass::CheckBindings()
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
			nvrhi::BindingSetItem::ConstantBuffer(4, m_Resources->m_HashGridCacheCB),

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

			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_Resources->m_HashBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_Resources->m_DecayTileBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_Resources->m_CellBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(3, m_Resources->m_MultibounceCellBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(4, m_Resources->m_UpdateTileBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(5, m_Resources->m_UpdateTileCountBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(6, m_Resources->m_VisibilityBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(7, m_Resources->m_VisibilityCountBuffer0),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(8, m_Resources->m_VisibilityCountBuffer1),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(9, m_Resources->m_VisibilityCellBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(10, m_Resources->m_MultibounceInfoBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(11, m_Resources->m_ProbeSpawnRadianceBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(12, m_Resources->m_ProbeSpawnSampleBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(13, m_Resources->m_ProbeSpawnBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(14, m_Resources->m_EmptyTileCountBuffer)
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GI12HashGridCachePass::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto bindingSet = m_BindingSets[currentSlot];
		if (!bindingSet)
			return;

		const uint32_t numTiles = m_Resources->m_NumTiles;
		const uint32_t numCells = m_Resources->m_NumCells;
		const uint32_t totalProbes = m_Resources->m_ProbeCount.x * m_Resources->m_ProbeCount.y;

		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, uint32_t gx, uint32_t gy, uint32_t gz) {
			if (!pipeline) return;
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(gx, gy, gz);
		};

		// 1. Purge stale cache tiles
		dispatch(m_PurgeTilesPipeline, (numTiles + 63) / 64, 1, 1);

		// 2. Trace rays and populate cells from empty screen probes
		dispatch(m_PopulateScreenProbesCellsPipeline, (totalProbes * 64 + 31) / 32, 1, 1);

		// 3. Populate multi-bounce cells if enabled
		if (Scene::GetSingleton()->m_Settings.GI12.UseMultibounce) {
			dispatch(m_PopulateMultibounceCellsPipeline, (numCells + 31) / 32, 1, 1);
		}

		// 4. Update tiles
		const uint32_t maxVisibilityEntries = totalProbes * 64;
		dispatch(m_UpdateTilesPipeline, (maxVisibilityEntries + 63) / 64, 1, 1);

		// 5. Update multi-bounce cells
		if (Scene::GetSingleton()->m_Settings.GI12.UseMultibounce) {
			dispatch(m_UpdateMultibounceCellsPipeline, (std::max(numCells, maxVisibilityEntries) + 31) / 32, 1, 1);
		}

		// 6. Resolve cells
		dispatch(m_ResolveCellsPipeline, (numCells + 63) / 64, 1, 1);
	}
}
