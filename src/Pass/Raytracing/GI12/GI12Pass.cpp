#include "GI12Pass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"

namespace Pass::Raytracing::GI12
{
	GI12Pass::GI12Pass(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		auto device = renderer->GetDevice();

		m_ConstantData = eastl::make_unique<GI12Data>();
		m_ConstantBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GI12Data), "GI12 Data", Constants::MAX_CB_VERSIONS));

		m_ScreenProbesData = eastl::make_unique<GI12ScreenProbesData>();
		m_ScreenProbesDataBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GI12ScreenProbesData), "GI12 Screen Probes", Constants::MAX_CB_VERSIONS));

		m_HashGridData = eastl::make_unique<GI12HashGridData>();
		m_HashGridDataBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GI12HashGridData), "GI12 Hash Grid", Constants::MAX_CB_VERSIONS));

		m_LinearClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));
		m_LinearWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_PointClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));
		m_PointWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(false));

		m_SceneTLAS->GetTopLevelAS().AddListener(this);

		SettingsChanged(Scene::GetSingleton()->m_Settings);
	}

	void GI12Pass::Initialize()
	{
		CreateBuffers();
		CreatePipeline();
	}

	void GI12Pass::CreateBuffers()
	{
		auto device = GetRenderer()->GetDevice();
		const auto& s = Scene::GetSingleton()->m_Settings.GI12Settings;

		// Size probe storage for the active resolution and spacing.
		const auto resolution = GetRenderer()->GetResolution();
		const uint32_t probeSize = eastl::min<uint32_t>(eastl::max<uint32_t>(s.ProbeSize, 4), 16);
		const uint32_t maxProbes = ((resolution.x + probeSize - 1) / probeSize) *
			((resolution.y + probeSize - 1) / probeSize);
		if (m_MaxProbes != maxProbes || m_ProbeSize != probeSize || !m_ProbePositionBuffer) {
			m_MaxProbes = maxProbes;
			m_ProbeSize = probeSize;
			m_ProbePositionBuffer = Util::CreateStructuredBuffer<float4>(device, maxProbes, "GI12 Probe Position", true);
			m_ProbeNormalBuffer = Util::CreateStructuredBuffer<float4>(device, maxProbes, "GI12 Probe Normal", true);
			m_ProbeLastUpdatedBuffer = Util::CreateStructuredBuffer<uint>(device, maxProbes, "GI12 Probe Last Updated", true);
			m_ProbeSHBuffer = Util::CreateStructuredBuffer<uint4>(device, maxProbes * 9, "GI12 Probe SH", true);
			m_ProbeRadianceBuffer = Util::CreateStructuredBuffer<float4>(device, maxProbes * probeSize * probeSize, "GI12 Probe Radiance", true);
			m_PreviousProbePositionBuffer = Util::CreateStructuredBuffer<float4>(device, maxProbes, "GI12 Previous Probe Position", true);
			m_PreviousProbeNormalBuffer = Util::CreateStructuredBuffer<float4>(device, maxProbes, "GI12 Previous Probe Normal", true);
			m_PreviousProbeSHBuffer = Util::CreateStructuredBuffer<uint4>(device, maxProbes * 9, "GI12 Previous Probe SH", true);
			m_PreviousProbeRadianceBuffer = Util::CreateStructuredBuffer<float4>(device, maxProbes * probeSize * probeSize, "GI12 Previous Probe Radiance", true);
			m_ResetCache = true;
		}

		// Capsaicin L2 stores four square mip regions in every directional tile.
		const uint32_t numBuckets = 1u << eastl::min<uint>(s.NumBuckets, MAX_BUCKETS_LOG2);
		const uint32_t tilesPerBucket = 1u << eastl::min<uint>(s.NumTilesPerBucket, MAX_TILES_PER_BUCKET_LOG2);
		const uint32_t numTiles = numBuckets * tilesPerBucket;
		constexpr uint32_t tileMip0Size = 8;
		constexpr uint32_t tileMip1Size = tileMip0Size / 2;
		constexpr uint32_t tileMip2Size = tileMip1Size / 2;
		constexpr uint32_t tileMip3Size = tileMip2Size / 2;
		constexpr uint32_t cellsPerTile = tileMip0Size * tileMip0Size + tileMip1Size * tileMip1Size +
			tileMip2Size * tileMip2Size + tileMip3Size * tileMip3Size;
		const uint32_t numCells = numTiles * cellsPerTile;
		if (m_NumCells != numCells || m_NumBuckets != numBuckets || m_NumTiles != numTiles || !m_HashBuffer) {
			m_NumBuckets = numBuckets;
			m_NumTiles = numTiles;
			m_NumCells = numCells;
			m_HashBuffer = Util::CreateStructuredBuffer<uint>(device, numTiles, "GI12 Hash", true);
			m_DecayTileBuffer = Util::CreateStructuredBuffer<uint>(device, numTiles, "GI12 Decay Tile", true);
			// Capsaicin ValueBuffer: uint2 half-packed radiance (packHalf4).
			m_ValueBuffer = Util::CreateStructuredBuffer<uint2>(device, numCells, "GI12 Value", true);
			m_ValueIndirectBuffer = Util::CreateStructuredBuffer<uint2>(device, numCells, "GI12 Value Indirect", true);
			// Scratch for atomic accumulation (numCells * 4 uints), resolved by UpdateTiles.
			m_UpdateCellValueBuffer = Util::CreateStructuredBuffer<uint>(device, numCells * 4, "GI12 Update Cell Value", true);
			m_UpdateCellValueIndirectBuffer = Util::CreateStructuredBuffer<uint>(device, numCells * 4, "GI12 Update Cell Value Indirect", true);
			m_PreviousHashBuffer = Util::CreateStructuredBuffer<uint>(device, numTiles, "GI12 Previous Hash", true);
			m_PreviousValueBuffer = Util::CreateStructuredBuffer<uint2>(device, numCells, "GI12 Previous Value", true);
			m_PreviousValueIndirectBuffer = Util::CreateStructuredBuffer<uint2>(device, numCells, "GI12 Previous Value Indirect", true);
			m_TileUpdateBuffer = Util::CreateStructuredBuffer<uint>(device, numTiles, "GI12 Tile Update", true);
			m_TileUpdateCountBuffer = Util::CreateStructuredBuffer<uint>(device, 1, "GI12 Tile Update Count", true);
			// Hash/prev-hash/decay/update uint per tile; Value* uint2 x4 (value,
			// indirect + previous); scratch uint x8 per cell; tile update count.
			const size_t bytes = numTiles * 16ull + numCells * 64ull + 4ull +
				maxProbes * (68ull + 18ull * 16ull + 2ull * probeSize * probeSize * 16ull + 4ull);
			logger::info("[VRAM] GI12 cache: {:.1f} MiB (probes {}, buckets {}, tiles {}, cells {})",
				bytes / 1048576.0, maxProbes, numBuckets, numTiles, numCells);
			m_ResetCache = true;
		}
	}

	void GI12Pass::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // b0: Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // b1: GI12
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // b2: ScreenProbes
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3), // b3: HashGrid
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(4), // b4: Raytracing
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(5), // b5: Features
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0), // t0: Scene TLAS
			nvrhi::BindingLayoutItem::Texture_SRV(1), // t1: depth
			nvrhi::BindingLayoutItem::Texture_SRV(2), // t2: sky
			nvrhi::BindingLayoutItem::Texture_SRV(3), // t3: normalRoughness
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4), // t4: lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5), // t5: previous hash
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6), // t6: previous cell radiance
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7), // t7: previous cell data
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8), // t8: previous probe position
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9), // t9: previous probe normal
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10), // t10: previous probe SH
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11), // t11: previous directional probe radiance
			nvrhi::BindingLayoutItem::Texture_SRV(12), // t12: albedo
			nvrhi::BindingLayoutItem::Texture_SRV(13), // t13: GNMAO
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(14), // t14: instances
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(15), // t15: meshes
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(16), // t16: transforms
			nvrhi::BindingLayoutItem::RawBuffer_SRV(17), // t17: mesh-slot remap
			nvrhi::BindingLayoutItem::RawBuffer_SRV(18), // t18: properties
			nvrhi::BindingLayoutItem::Texture_SRV(19), // t19: water displacement
			nvrhi::BindingLayoutItem::Texture_SRV(20), // t20: skin detail normal
			nvrhi::BindingLayoutItem::Texture_SRV(21), // t21: projection noise
			nvrhi::BindingLayoutItem::Texture_SRV(22), // t22: water flow
			nvrhi::BindingLayoutItem::Texture_SRV(23), // t23: motion vectors
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			nvrhi::BindingLayoutItem::Sampler(3),
			nvrhi::BindingLayoutItem::Sampler(4),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // u0: probe radiance
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1), // u1: probe SH
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2), // u2: probe normal
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3), // u3: hash
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5), // u5: tile last touched
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6), // u6: tile update list
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7), // u7: tile update count
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(8), // u8: directional probe radiance
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(9), // u9: probe update frame
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(10), // u10: value
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(11), // u11: value indirect
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(12), // u12: update cell value
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(13), // u13: update cell value indirect
		};
		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void GI12Pass::CreatePipeline()
	{
		CreateBindingLayout();
		auto device = GetRenderer()->GetDevice();
		auto defines = Util::Shader::GetDXCDefines(m_Defines);

		auto build = [&](const wchar_t* path, const char* name, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, path, defines, ShaderStage::Compute);
			if (!blob) {
				logger::error("GI12Pass::CreatePipeline - Failed to compile {} shader.", name);
				return;
			}
			shader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("GI12Pass::CreatePipeline - Failed to create {} shader.", name);
				return;
			}
			auto pipelineDesc = nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout);
			if (eastl::string_view(name) == "update") {
				auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
				pipelineDesc
					.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
					.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
					.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
					.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
					.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
					.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);
			}
			pipeline = device->createComputePipeline(pipelineDesc);
		};

		build(L"data/shaders/raytracing/GI12/GI12Clear.hlsl", "clear", m_ClearShader, m_ClearPipeline);
		build(L"data/shaders/raytracing/GI12/GI12Update.hlsl", "update", m_UpdateShader, m_UpdatePipeline);
		build(L"data/shaders/raytracing/GI12/GI12UpdateTiles.hlsl", "updatetiles", m_UpdateTilesShader, m_UpdateTilesPipeline);

		CreateResolveLayout(Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
			Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);

		m_ResolveShader = nullptr;
		m_ResolvePipeline = nullptr;
		{
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/GI12/GI12Resolve.hlsl", defines, ShaderStage::Compute);
			if (!blob) {
				logger::error("GI12Pass::CreatePipeline - Failed to compile resolve shader.");
			} else {
				m_ResolveShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
				if (!m_ResolveShader) {
					logger::error("GI12Pass::CreatePipeline - Failed to create resolve shader.");
				} else {
					m_ResolvePipeline = device->createComputePipeline(
						nvrhi::ComputePipelineDesc().setComputeShader(m_ResolveShader).addBindingLayout(m_ResolveLayout));
				}
			}
		}
	}

	void GI12Pass::CreateResolveLayout(bool specular)
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0), // s0: linear clamp
			nvrhi::BindingLayoutItem::Sampler(1), // s1: point clamp
			nvrhi::BindingLayoutItem::Sampler(2), // s2: linear wrap
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // b0: Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // b1: GI12
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // b2: ScreenProbes
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3), // b3: HashGrid
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(4), // b4: Raytracing
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(5), // b5: Features
			nvrhi::BindingLayoutItem::Texture_SRV(0), // t0: depth
			nvrhi::BindingLayoutItem::Texture_SRV(1), // t1: albedo
			nvrhi::BindingLayoutItem::Texture_SRV(2), // t2: normalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(3), // t3: gnmao
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4), // t4: probe SH
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5), // t5: cell radiance
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6), // t6: cell data
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7), // t7: sparse cell hash
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8), // t8: probe position
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9), // t9: probe normal
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(10), // t10: scene TLAS
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11), // t11: directional probe radiance
			nvrhi::BindingLayoutItem::Texture_SRV(12), // t12: sky hemisphere
			nvrhi::BindingLayoutItem::Texture_SRV(13), // t13: geometric face normals
			nvrhi::BindingLayoutItem::Texture_UAV(0), // u0: diffuse output
		};
		if (specular)
			desc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1)); // u1: specular output
		m_ResolveLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
		m_ResolveSpecular = specular;
	}

	void GI12Pass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		if (m_Enabled)
			CreateBuffers();
		m_ResetCache = true;
		m_BindingSetDirty.fill(true);
		m_ResolveDirty.fill(true);
	}

	void GI12Pass::SettingsChanged(const Settings& settings)
	{
		const auto& s = settings.GI12Settings;
		m_Enabled = s.Enabled;

		auto defines = Util::Shader::GetGI12Defines(settings);
		const bool definesChanged = defines != m_Defines;
		if (definesChanged) {
			m_Defines = defines;
		}
		if (m_Enabled) {
			CreateBuffers();
			if (definesChanged || !m_HashBuffer) {
				CreatePipeline();
				m_BindingSets.fill(nullptr);
				m_BindingSetDirty.fill(true);
				m_ResolveSets.fill(nullptr);
				m_ResolveDirty.fill(true);
			}
		}

		if (!m_Enabled) {
			m_BindingSets.fill(nullptr);
			m_ResolveSets.fill(nullptr);
			m_ResetCache = true;
			return;
		}

		m_ResetCache = true;
		m_BindingSetDirty.fill(true);
		m_ResolveDirty.fill(true);
	}

	void GI12Pass::UpdateConstants()
	{
		const auto& s = Scene::GetSingleton()->m_Settings.GI12Settings;
		auto* renderer = GetRenderer();
		const auto res = renderer->GetResolution();

		m_ConstantData->Enabled = 1;
		m_ConstantData->ScreenProbes = s.ScreenProbes ? 1 : 0;
		m_ConstantData->HashGridCache = s.HashGridCache ? 1 : 0;
		m_ConstantData->GlossyReflections = s.GlossyReflections ? 1 : 0;
		m_ConstantData->Pad0.x = s.Multibounce ? 1u : 0u;
		m_ConstantData->Pad0.y = renderer->GetMotionVectorTexture() != nullptr ? 1u : 0u;
		m_ConstantData->FrameIndex = m_FrameIndex;
		m_ConstantData->NumLights = static_cast<uint>(eastl::min<size_t>(
			Scene::GetSingleton()->GetSceneGraph()->GetLights().size(), Constants::LIGHTS_MAX));

		const uint probeSize = eastl::min<uint>(eastl::max<uint>(s.ProbeSize, 4), 16);
		m_ScreenProbesData->CellSize = 1.0f;
		m_ScreenProbesData->ProbeSize = probeSize;
		m_ScreenProbesData->ProbeCount = {
			(res.x + probeSize - 1) / probeSize,
			(res.y + probeSize - 1) / probeSize
		};
		m_ScreenProbesData->ProbeSpawnTileSize = probeSize * 2; // QuarterSpp like Capsaicin
		m_ScreenProbesData->ProbeMaskMipCount = 4;

		const float tileCellRatio = 8.0f; // Capsaicin default
		m_HashGridData->CellSize = s.CellSize;
		m_HashGridData->MinCellSize = s.MinCellSize;
		m_HashGridData->TileSize = s.MinCellSize * tileCellRatio;
		m_HashGridData->TileCellRatio = tileCellRatio;
		m_HashGridData->NumBuckets = m_NumBuckets;
		m_HashGridData->NumTiles = m_NumTiles;
		m_HashGridData->NumCells = m_NumCells;
		m_HashGridData->NumTilesPerBucket = 1u << eastl::min<uint>(s.NumTilesPerBucket, MAX_TILES_PER_BUCKET_LOG2);
		// Exact Capsaicin sizing: sizeMip0 = tileCellRatio, each mip halves.
		// numCellsPerTileMip* and first offsets match gi1.cpp ensureMemoryIsAllocated.
		m_HashGridData->SizeTileMip0 = 8;
		m_HashGridData->SizeTileMip1 = 4;
		m_HashGridData->SizeTileMip2 = 2;
		m_HashGridData->SizeTileMip3 = 1;
		m_HashGridData->NumCellsPerTileMip0 = 64;
		m_HashGridData->NumCellsPerTileMip1 = 16;
		m_HashGridData->NumCellsPerTileMip2 = 4;
		m_HashGridData->NumCellsPerTileMip3 = 1;
		m_HashGridData->NumCellsPerTile = 85;
		m_HashGridData->FirstCellOffsetTileMip0 = 0;
		m_HashGridData->FirstCellOffsetTileMip1 = 64;
		m_HashGridData->FirstCellOffsetTileMip2 = 80;
		m_HashGridData->FirstCellOffsetTileMip3 = 84;
		m_HashGridData->BufferPingPong = m_FrameIndex & 1;
		m_HashGridData->MaxSampleCount = s.MaxSampleCount;
		m_HashGridData->DiscardMultibounceProbability = s.MultibounceDiscardProbability;
		m_HashGridData->MaxMultibounceSampleCount = s.MaxSampleCount;
		m_HashGridData->DebugMipLevel = 0;
		m_HashGridData->FrameIndex = m_FrameIndex;
	}

	void GI12Pass::CheckBindings()
	{
		uint32_t slot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[slot] && m_BindingSets[slot])
			return;

		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();
		auto* renderTargets = renderer->GetRenderTargets();
		auto* motionVectors = renderer->GetMotionVectorTexture();

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(2, m_ScreenProbesDataBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_HashGridDataBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(4, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(5, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(3, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, scene->GetSceneGraph()->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, m_PreviousHashBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, m_PreviousValueBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(7, m_PreviousValueIndirectBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(8, m_PreviousProbePositionBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(9, m_PreviousProbeNormalBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(10, m_PreviousProbeSHBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(11, m_PreviousProbeRadianceBuffer),
			nvrhi::BindingSetItem::Texture_SRV(12, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(13, renderTargets->gnmao),
			nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(14, scene->GetSceneGraph()->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(15, scene->GetSceneGraph()->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(16, scene->GetSceneGraph()->GetTransformBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(17, scene->GetSceneGraph()->GetMeshSlotRemapBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(18, scene->GetSceneGraph()->GetPropertiesBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(19, renderer->GetWaterDisplacementTexture()),
			nvrhi::BindingSetItem::Texture_SRV(20, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::Texture_SRV(21, scene->GetProjNoiseTexture()),
			nvrhi::BindingSetItem::Texture_SRV(22, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::Texture_SRV(23, motionVectors ? motionVectors : renderer->GetBlackTexture()),
			nvrhi::BindingSetItem::Sampler(1, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Sampler(2, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(3, m_PointWrapSampler),
			nvrhi::BindingSetItem::Sampler(4, m_PointClampSampler),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_ProbePositionBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_ProbeSHBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_ProbeNormalBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(3, m_HashBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(5, m_DecayTileBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(6, m_TileUpdateBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(7, m_TileUpdateCountBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(8, m_ProbeRadianceBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(9, m_ProbeLastUpdatedBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(10, m_ValueBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(11, m_ValueIndirectBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(12, m_UpdateCellValueBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(13, m_UpdateCellValueIndirectBuffer),
		};
		m_BindingSets[slot] = GetRenderer()->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[slot] = false;
	}

	void GI12Pass::CheckResolveBindings()
	{
		uint32_t slot = GetRenderer()->GetCurrentSlot();
		if (!m_ResolveDirty[slot] && m_ResolveSets[slot])
			return;

		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();
		auto* renderTargets = renderer->GetRenderTargets();
		auto& textureManager = renderer->RenderTargetManager();

		const bool nrd = (scene->m_Settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
			scene->m_Settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);

		nvrhi::ITexture* diffuseTexture = nrd
			? textureManager.GetTexture(RenderTarget::DiffuseRadiance)
			: renderer->GetMainTexture();

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(1, m_PointClampSampler),
			nvrhi::BindingSetItem::Sampler(2, m_LinearWrapSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(2, m_ScreenProbesDataBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_HashGridDataBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(4, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(5, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(1, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(2, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(3, renderTargets->gnmao),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_ProbeSHBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, m_ValueBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, m_ValueIndirectBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(7, m_HashBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(8, m_ProbePositionBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(9, m_ProbeNormalBuffer),
			nvrhi::BindingSetItem::RayTracingAccelStruct(10, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(11, m_ProbeRadianceBuffer),
			nvrhi::BindingSetItem::Texture_SRV(12, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(13, textureManager.GetTexture(RenderTarget::FaceNormals)),
			nvrhi::BindingSetItem::Texture_UAV(0, diffuseTexture),
		};
		if (nrd && m_ResolveSpecular)
			desc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(1, textureManager.GetTexture(RenderTarget::SpecularRadiance)));

		m_ResolveSets[slot] = GetRenderer()->GetDevice()->createBindingSet(desc, m_ResolveLayout);
		m_ResolveDirty[slot] = false;
	}

	void GI12Pass::ClearCache(nvrhi::ICommandList* commandList)
	{
		commandList->clearBufferUInt(m_ProbePositionBuffer, 0);
		commandList->clearBufferUInt(m_ProbeNormalBuffer, 0);
		commandList->clearBufferUInt(m_ProbeLastUpdatedBuffer, 0);
		commandList->clearBufferUInt(m_ProbeSHBuffer, 0);
		commandList->clearBufferUInt(m_ProbeRadianceBuffer, 0);
		commandList->clearBufferUInt(m_PreviousProbePositionBuffer, 0);
		commandList->clearBufferUInt(m_PreviousProbeNormalBuffer, 0);
		commandList->clearBufferUInt(m_PreviousProbeSHBuffer, 0);
		commandList->clearBufferUInt(m_PreviousProbeRadianceBuffer, 0);
		commandList->clearBufferUInt(m_HashBuffer, 0);
		commandList->clearBufferUInt(m_DecayTileBuffer, 0);
		commandList->clearBufferUInt(m_ValueBuffer, 0);
		commandList->clearBufferUInt(m_ValueIndirectBuffer, 0);
		commandList->clearBufferUInt(m_UpdateCellValueBuffer, 0);
		commandList->clearBufferUInt(m_UpdateCellValueIndirectBuffer, 0);
		commandList->clearBufferUInt(m_PreviousHashBuffer, 0);
		commandList->clearBufferUInt(m_PreviousValueBuffer, 0);
		commandList->clearBufferUInt(m_PreviousValueIndirectBuffer, 0);
		commandList->clearBufferUInt(m_TileUpdateBuffer, 0);
		commandList->clearBufferUInt(m_TileUpdateCountBuffer, 0);
		m_FrameIndex = 0;
		m_ResetCache = false;
	}

	void GI12Pass::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled || !m_ClearPipeline || !m_UpdatePipeline)
			return;

		const auto revision = Scene::GetSingleton()->GetLightingRevision();
		if (m_ResetCache || m_LightingRevision != revision) {
			ClearCache(commandList);
			m_LightingRevision = revision;
		}

		UpdateConstants();
		commandList->writeBuffer(m_ConstantBuffer, m_ConstantData.get(), sizeof(GI12Data));
		commandList->writeBuffer(m_ScreenProbesDataBuffer, m_ScreenProbesData.get(), sizeof(GI12ScreenProbesData));
		commandList->writeBuffer(m_HashGridDataBuffer, m_HashGridData.get(), sizeof(GI12HashGridData));

		CheckBindings();
		uint32_t slot = GetRenderer()->GetCurrentSlot();
		nvrhi::BindingSetVector bindings = { m_BindingSets[slot] };
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
		nvrhi::BindingSetVector updateBindings = {
			m_BindingSets[slot],
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
		};

		// Resolve FIRST: it reads last frame's cache. The fill below then
		// produces this frame's data. Same-frame read-after-write would let
		// every pixel echo its own unconverged sky sample back (flat blue
		// wash, no occlusion differentiation). One frame of lag is inherent
		// to cache-based GI (cf. Capsaicin) and invisible in practice.
		if (m_ResolvePipeline) {
			CheckResolveBindings();

			nvrhi::ComputeState resolveState;
			resolveState.pipeline = m_ResolvePipeline;
			resolveState.bindings = { m_ResolveSets[slot] };
			commandList->setComputeState(resolveState);

			const auto resolution = Renderer::GetSingleton()->GetScaledDynamicResolution();
			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}

		// Clear decays the cache. Copy the decayed state into an immutable
		// snapshot before fill so multibounce reads cannot race with writes.
		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, bool surfaceBindings = false) {
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = surfaceBindings ? updateBindings : bindings;
			commandList->setComputeState(state);
		};

		{
			// Clear covers tiles (purge) + probe history (decay); Value cells
			// need no per-frame visit (UpdateTiles merges touched tiles only).
			const uint32_t probeElements = m_MaxProbes * m_ProbeSize * m_ProbeSize;
			const uint32_t shElements = m_MaxProbes * 9u;
			const uint32_t groups = (eastl::max(m_NumTiles, eastl::max(probeElements, shElements)) + 63) / 64;
			dispatch(m_ClearPipeline);
			commandList->dispatch(eastl::max<uint32_t>(groups, 1));
		}
		commandList->copyBuffer(m_PreviousHashBuffer, 0, m_HashBuffer, 0, m_NumTiles * sizeof(uint));
		commandList->copyBuffer(m_PreviousValueBuffer, 0, m_ValueBuffer, 0, m_NumCells * sizeof(uint2));
		commandList->copyBuffer(m_PreviousValueIndirectBuffer, 0, m_ValueIndirectBuffer, 0, m_NumCells * sizeof(uint2));
		commandList->copyBuffer(m_PreviousProbePositionBuffer, 0, m_ProbePositionBuffer, 0, m_MaxProbes * sizeof(float4));
		commandList->copyBuffer(m_PreviousProbeNormalBuffer, 0, m_ProbeNormalBuffer, 0, m_MaxProbes * sizeof(float4));
		commandList->copyBuffer(m_PreviousProbeSHBuffer, 0, m_ProbeSHBuffer, 0, m_MaxProbes * 9u * sizeof(uint4));
		commandList->copyBuffer(m_PreviousProbeRadianceBuffer, 0, m_ProbeRadianceBuffer, 0,
			m_MaxProbes * m_ProbeSize * m_ProbeSize * sizeof(float4));
		{
			dispatch(m_UpdatePipeline, true);
			const uint32_t probesPerSpawnTile = m_ConstantData->ScreenProbes != 0
				? eastl::max<uint32_t>(m_ScreenProbesData->ProbeSpawnTileSize / m_ScreenProbesData->ProbeSize, 1u)
				: 1u;
			const uint32_t spawnTilesX = (m_ScreenProbesData->ProbeCount.x + probesPerSpawnTile - 1u) /
				probesPerSpawnTile;
			const uint32_t spawnTilesY = (m_ScreenProbesData->ProbeCount.y + probesPerSpawnTile - 1u) /
				probesPerSpawnTile;
			commandList->dispatch(eastl::max<uint32_t>(spawnTilesX * spawnTilesY, 1u));
		}
		// UpdateTiles merges per-frame scratch into the persistent Value
		// buffers (Capsaicin gi1.comp). One 8x8 group per tile slot; groups
		// past the per-frame update count exit immediately.
		if (m_UpdateTilesPipeline) {
			dispatch(m_UpdateTilesPipeline);
			commandList->dispatch(eastl::max<uint32_t>(m_NumTiles, 1u));
		}
		m_FrameIndex++;
	}
}
