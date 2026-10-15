#include "GI12Resources.h"

#include "Renderer.h"
#include "Scene.h"
#include "Util.h"
#include "Utils/Game.h"

namespace Pass::Raytracing::GI12
{
	GI12Resources::GI12Resources(Renderer* renderer)
		: m_Renderer(renderer)
	{
	}

	void GI12Resources::Initialize()
	{
		auto device = m_Renderer->GetDevice();

		m_LinearWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_LinearClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_PointWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(false));

		m_PointClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));

		m_GI12CB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GI12Constants), "GI12 Constants", Constants::MAX_CB_VERSIONS));

		m_ScreenProbesCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(ScreenProbesConstants), "Screen Probes Constants", Constants::MAX_CB_VERSIONS));

		m_HashGridCacheCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(HashGridCacheConstants), "Hash Grid Cache Constants", Constants::MAX_CB_VERSIONS));

		m_WorldSpaceReSTIRCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(WorldSpaceReSTIRConstants), "World Space ReSTIR Constants", Constants::MAX_CB_VERSIONS));

		m_GlossyReflectionsCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GlossyReflectionsConstants), "Glossy Reflections Constants", Constants::MAX_CB_VERSIONS));

		m_GlossyAtrousCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GlossyReflectionsAtrousConstants), "Glossy Atrous Constants", Constants::MAX_CB_VERSIONS));

		m_GIDenoiserCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(GIDenoiserConstants), "GI Denoiser Constants", Constants::MAX_CB_VERSIONS));
	}

	void GI12Resources::ResolutionChanged(uint2 resolution)
	{
		if (m_Resolution.x == resolution.x && m_Resolution.y == resolution.y)
			return;

		m_Resolution = resolution;
		auto device = m_Renderer->GetDevice();

		// Screen Probes
		m_ProbeCount = uint2((resolution.x + 7) / 8, (resolution.y + 7) / 8);
		const uint32_t totalProbes = m_ProbeCount.x * m_ProbeCount.y;

		auto createUavTexture = [&](uint32_t w, uint32_t h, nvrhi::Format format, const char* name) -> nvrhi::TextureHandle {
			return device->createTexture(
				nvrhi::TextureDesc()
				.setDimension(nvrhi::TextureDimension::Texture2D)
				.setWidth(w)
				.setHeight(h)
				.setFormat(format)
				.setIsUAV(true)
				.setInitialState(nvrhi::ResourceStates::Common)
				.setKeepInitialState(true)
				.setDebugName(name));
		};

		auto createStructuredBuffer = [&](size_t byteSize, size_t structStride, const char* name) -> nvrhi::BufferHandle {
			return device->createBuffer(
				nvrhi::BufferDesc()
				.setByteSize(byteSize)
				.setStructStride(static_cast<uint32_t>(structStride))
				.setCanHaveUAVs(true)
				.setInitialState(nvrhi::ResourceStates::Common)
				.setKeepInitialState(true)
				.setDebugName(name));
		};

		m_ProbeMaskBuffer = createUavTexture(m_ProbeCount.x, m_ProbeCount.y, nvrhi::Format::R32_UINT, "GI12_ProbeMaskBuffer");
		m_PreviousProbeMaskBuffer = createUavTexture(m_ProbeCount.x, m_ProbeCount.y, nvrhi::Format::R32_UINT, "GI12_PreviousProbeMaskBuffer");
		m_ProbeBuffer = createUavTexture(m_ProbeCount.x * 8, m_ProbeCount.y * 8, nvrhi::Format::RGBA16_FLOAT, "GI12_ProbeBuffer");
		m_PreviousProbeBuffer = createUavTexture(m_ProbeCount.x * 8, m_ProbeCount.y * 8, nvrhi::Format::RGBA16_FLOAT, "GI12_PreviousProbeBuffer");

		m_ProbeSHBuffer = createStructuredBuffer(totalProbes * 9 * sizeof(uint2), sizeof(uint2), "GI12_ProbeSHBuffer");
		m_PreviousProbeSHBuffer = createStructuredBuffer(totalProbes * 9 * sizeof(uint2), sizeof(uint2), "GI12_PreviousProbeSHBuffer");
		m_ProbeSpawnBuffer = createStructuredBuffer(totalProbes * sizeof(uint32_t), sizeof(uint32_t), "GI12_ProbeSpawnBuffer");
		m_ProbeSpawnSampleBuffer = createStructuredBuffer(totalProbes * 64 * sizeof(uint2), sizeof(uint2), "GI12_ProbeSpawnSampleBuffer");
		m_ProbeSpawnRadianceBuffer = createStructuredBuffer(totalProbes * 64 * sizeof(uint2), sizeof(uint2), "GI12_ProbeSpawnRadianceBuffer");
		m_EmptyTileBuffer = createStructuredBuffer(totalProbes * sizeof(uint32_t), sizeof(uint32_t), "GI12_EmptyTileBuffer");
		m_EmptyTileCountBuffer = createStructuredBuffer(1 * sizeof(uint32_t), sizeof(uint32_t), "GI12_EmptyTileCountBuffer");
		m_ProbeCachedTileLRUBuffer = createStructuredBuffer(totalProbes * sizeof(uint32_t), sizeof(uint32_t), "GI12_ProbeCachedTileLRUBuffer");

		// Hash Grid Cache
		const uint32_t numBuckets = 4096;
		const uint32_t numTilesPerBucket = 16;
		m_NumTiles = numBuckets * numTilesPerBucket; // 65,536 tiles
		m_NumCells = m_NumTiles * 85;                 // 5,570,560 cells

		m_HashBuffer = createStructuredBuffer(m_NumTiles * sizeof(uint32_t), sizeof(uint32_t), "GI12_HashBuffer");
		m_DecayTileBuffer = createStructuredBuffer(m_NumTiles * sizeof(uint32_t), sizeof(uint32_t), "GI12_DecayTileBuffer");
		m_CellBuffer = createStructuredBuffer(m_NumCells * sizeof(uint4), sizeof(uint4), "GI12_CellBuffer");
		m_CellPackedBuffer = createStructuredBuffer(m_NumCells * sizeof(uint4), sizeof(uint4), "GI12_CellPackedBuffer");
		m_MultibounceCellBuffer = createStructuredBuffer(m_NumCells * sizeof(uint4), sizeof(uint4), "GI12_MultibounceCellBuffer");
		m_UpdateTileBuffer = createStructuredBuffer(m_NumTiles * sizeof(uint32_t), sizeof(uint32_t), "GI12_UpdateTileBuffer");
		m_UpdateTileCountBuffer = createStructuredBuffer(1 * sizeof(uint32_t), sizeof(uint32_t), "GI12_UpdateTileCountBuffer");
		m_VisibilityBuffer = createStructuredBuffer(m_NumCells * sizeof(float4), sizeof(float4), "GI12_VisibilityBuffer");
		m_VisibilityCountBuffer0 = createStructuredBuffer(1 * sizeof(uint32_t), sizeof(uint32_t), "GI12_VisibilityCountBuffer0");
		m_VisibilityCountBuffer1 = createStructuredBuffer(1 * sizeof(uint32_t), sizeof(uint32_t), "GI12_VisibilityCountBuffer1");
		m_VisibilityCellBuffer = createStructuredBuffer(m_NumCells * sizeof(uint32_t), sizeof(uint32_t), "GI12_VisibilityCellBuffer");
		m_MultibounceInfoBuffer = createStructuredBuffer(m_NumCells * sizeof(uint2), sizeof(uint2), "GI12_MultibounceInfoBuffer");

		// World Space ReSTIR
		const uint32_t kReSTIREntries = ::GI12::Constants::kNumEntries;
		m_ReservoirBuffer = createStructuredBuffer(kReSTIREntries * sizeof(uint4), sizeof(uint4), "GI12_ReservoirBuffer");
		m_MultibounceReservoirBuffer = createStructuredBuffer(kReSTIREntries * sizeof(uint4), sizeof(uint4), "GI12_MultibounceReservoirBuffer");

		// Glossy Reflections
		const uint2 halfRes = uint2((resolution.x + 1) / 2, (resolution.y + 1) / 2);
		m_RtSampleBuffer = createStructuredBuffer(resolution.x * resolution.y * sizeof(uint32_t), sizeof(uint32_t), "GI12_RtSampleBuffer");
		m_RtSampleCountBuffer = createStructuredBuffer(sizeof(uint32_t), sizeof(uint32_t), "GI12_RtSampleCountBuffer");

		m_SpecularBuffer = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::RGBA16_FLOAT, "GI12_SpecularBuffer");
		m_DirectionBuffer = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::RGBA16_FLOAT, "GI12_DirectionBuffer");
		m_ReflectionsBuffer0 = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::RGBA16_FLOAT, "GI12_ReflectionsBuffer0");
		m_AverageSquaredBuffer0 = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::RGBA16_FLOAT, "GI12_AverageSquaredBuffer0");
		m_ReflectionsBuffer1 = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::RGBA16_FLOAT, "GI12_ReflectionsBuffer1");
		m_AverageSquaredBuffer1 = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::RGBA16_FLOAT, "GI12_AverageSquaredBuffer1");
		m_FirefliesBuffer = createUavTexture(halfRes.x, halfRes.y, nvrhi::Format::R16_FLOAT, "GI12_FirefliesBuffer");
		m_StandardDevBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::RGBA16_FLOAT, "GI12_StandardDevBuffer");
		m_ReflectionBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::RGBA16_FLOAT, "GI12_ReflectionBuffer");
		m_PreviousReflectionBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::RGBA16_FLOAT, "GI12_PreviousReflectionBuffer");

		// GI Denoiser
		m_BlurMask = createUavTexture(resolution.x, resolution.y, nvrhi::Format::R16_FLOAT, "GI12_BlurMask");
		m_ColorBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::RGBA16_FLOAT, "GI12_ColorBuffer");
		m_ColorDeltaBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::R16_FLOAT, "GI12_ColorDeltaBuffer");
		m_PreviousColorBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::RGBA16_FLOAT, "GI12_PreviousColorBuffer");
		m_PreviousColorDeltaBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::R16_FLOAT, "GI12_PreviousColorDeltaBuffer");
		m_IrradianceBuffer = createUavTexture(resolution.x, resolution.y, nvrhi::Format::RGBA16_FLOAT, "GI12_IrradianceBuffer");
	}

	void GI12Resources::UpdateConstants(nvrhi::ICommandList* commandList, const Settings& settings)
	{
		const auto* cameraData = Scene::GetSingleton()->GetCameraData();
		if (!cameraData)
			return;

		GI12Constants gi12CB{};
		gi12CB.view_proj = cameraData->ViewProj;
		gi12CB.view_proj_prev = cameraData->PrevViewProj;

		DirectX::XMMATRIX vp = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&cameraData->ViewProj));
		DirectX::XMMATRIX vpInv = DirectX::XMMatrixInverse(nullptr, vp);
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gi12CB.view_proj_inv), vpInv);

		DirectX::XMMATRIX vpPrev = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&cameraData->PrevViewProj));
		DirectX::XMMATRIX vpInvPrev = DirectX::XMMatrixInverse(nullptr, vpPrev);
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gi12CB.view_proj_inv_prev), vpInvPrev);

		DirectX::XMMATRIX reproj = DirectX::XMMatrixMultiply(vpInv, vpPrev);
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gi12CB.reprojection), reproj);

		gi12CB.eye = cameraData->Position;
		gi12CB.frame_index = m_FrameIndex;
		gi12CB.eye_prev = cameraData->PositionPrev;
		gi12CB.use_direct_lighting = settings.GI12.UseDirectLighting ? 1 : 0;
		gi12CB.buffer_dimensions = uint2(m_Resolution.x, m_Resolution.y);
		gi12CB.near_far = float2(cameraData->CameraData.x, cameraData->CameraData.y);
		gi12CB.use_multibounce = settings.GI12.UseMultibounce ? 1 : 0;
		gi12CB.use_temporal_feedback = settings.GI12.UseTemporalFeedback ? 1 : 0;

		commandList->writeBuffer(m_GI12CB, &gi12CB, sizeof(gi12CB));

		// Screen Probes CB
		ScreenProbesConstants spCB{};
		const float probeAngularFootprint = (m_Resolution.y > 0)
			? (cameraData->NDCToView.y / static_cast<float>(m_Resolution.y)) * static_cast<float>(::GI12::Constants::kProbeSize)
			: 0.01136f;
		spCB.cell_size = probeAngularFootprint * settings.GI12.ScreenProbesCellSize;
		spCB.probe_size = ::GI12::Constants::kProbeSize;
		spCB.probe_count = m_ProbeCount;
		spCB.probe_mask_mip_count = 1;
		spCB.probe_spawn_tile_size = ::GI12::Constants::kProbeSpawnTileSize;
		spCB.debug_mode = 0;
		commandList->writeBuffer(m_ScreenProbesCB, &spCB, sizeof(spCB));

		// Hash Grid Cache CB
		HashGridCacheConstants hgCB{};
		hgCB.cell_size = 1.0f / settings.GI12.HashGridCellSize;
		hgCB.min_cell_size = settings.GI12.HashGridMinCellSize * Util::Units::M_TO_GAME_UNIT;
		hgCB.tile_size = 8.0f;
		hgCB.tile_cell_ratio = 8.0f;
		hgCB.num_buckets = 4096;
		hgCB.num_tiles = m_NumTiles;
		hgCB.num_cells = m_NumCells;
		hgCB.num_tiles_per_bucket = 16;
		hgCB.size_tile_mip0 = 8;
		hgCB.size_tile_mip1 = 4;
		hgCB.size_tile_mip2 = 2;
		hgCB.size_tile_mip3 = 1;
		hgCB.num_cells_per_tile_mip0 = 64;
		hgCB.num_cells_per_tile_mip1 = 16;
		hgCB.num_cells_per_tile_mip2 = 4;
		hgCB.num_cells_per_tile_mip3 = 1;
		hgCB.num_cells_per_tile = 85;
		hgCB.first_cell_offset_tile_mip0 = 0;
		hgCB.first_cell_offset_tile_mip1 = 64;
		hgCB.first_cell_offset_tile_mip2 = 80;
		hgCB.first_cell_offset_tile_mip3 = 84;
		hgCB.buffer_ping_pong = (m_FrameIndex & 1);
		hgCB.max_sample_count = 128.0f;
		hgCB.discard_multibounce_ray_probability = 0.05f;
		hgCB.max_multibounce_sample_count = 64.0f;
		hgCB.debug_mip_level = 0;
		hgCB.debug_propagate = 0;
		hgCB.debug_max_cell_decay = 50;
		hgCB.debug_bucket_occupancy_histogram_size = 17;
		hgCB.debug_bucket_overflow_histogram_size = 17;
		hgCB.debug_mode = 0;
		commandList->writeBuffer(m_HashGridCacheCB, &hgCB, sizeof(hgCB));

		// World Space ReSTIR CB
		WorldSpaceReSTIRConstants wsCB{};
		wsCB.cell_size = 1.0f / settings.GI12.ReservoirCacheCellSize;
		wsCB.num_cells = ::GI12::Constants::kNumCells;
		wsCB.num_entries_per_cell = ::GI12::Constants::kNumEntriesPerCell;
		commandList->writeBuffer(m_WorldSpaceReSTIRCB, &wsCB, sizeof(wsCB));

		// Glossy Reflections CB
		GlossyReflectionsConstants grCB{};
		grCB.full_res = uint2(m_Resolution.x, m_Resolution.y);
		grCB.full_radius = 7;
		grCB.half_radius = 4;
		grCB.mark_fireflies_half_radius = 3;
		grCB.mark_fireflies_full_radius = 5;
		grCB.mark_fireflies_half_low_threshold = 2.0f;
		grCB.mark_fireflies_full_low_threshold = 2.0f;
		grCB.mark_fireflies_half_high_threshold = 4.0f;
		grCB.mark_fireflies_full_high_threshold = 4.0f;
		grCB.cleanup_fireflies_half_radius = 2;
		grCB.cleanup_fireflies_full_radius = 4;
		grCB.low_roughness_threshold = settings.GI12.GlossyLowRoughnessThreshold;
		grCB.high_roughness_threshold = settings.GI12.GlossyHighRoughnessThreshold;
		grCB.half_res = 1;
		commandList->writeBuffer(m_GlossyReflectionsCB, &grCB, sizeof(grCB));

		// Glossy Atrous CB (initial frame state)
		GlossyReflectionsAtrousConstants gaCB{};
		gaCB.full_step = 1;
		gaCB.ping_pong = 0;
		gaCB.pass_index = 0;
		commandList->writeBuffer(m_GlossyAtrousCB, &gaCB, sizeof(gaCB));

		// GI Denoiser CB (initial frame state)
		GIDenoiserConstants gdCB{};
		gdCB.blur_direction_x = 1;
		gdCB.blur_direction_y = 0;
		gdCB.pass_index = 0;
		commandList->writeBuffer(m_GIDenoiserCB, &gdCB, sizeof(gdCB));

		// Reset per-frame atomic counters
		uint32_t zero = 0;
		if (m_EmptyTileCountBuffer) commandList->writeBuffer(m_EmptyTileCountBuffer, &zero, sizeof(zero));
		if (m_VisibilityCountBuffer0) commandList->writeBuffer(m_VisibilityCountBuffer0, &zero, sizeof(zero));
		if (m_VisibilityCountBuffer1) commandList->writeBuffer(m_VisibilityCountBuffer1, &zero, sizeof(zero));
		if (m_UpdateTileCountBuffer) commandList->writeBuffer(m_UpdateTileCountBuffer, &zero, sizeof(zero));
		if (m_RtSampleCountBuffer) commandList->writeBuffer(m_RtSampleCountBuffer, &zero, sizeof(zero));
	}

	void GI12Resources::SwapHistory()
	{
		std::swap(m_ProbeMaskBuffer, m_PreviousProbeMaskBuffer);
		std::swap(m_ProbeBuffer, m_PreviousProbeBuffer);
		std::swap(m_ProbeSHBuffer, m_PreviousProbeSHBuffer);
		std::swap(m_ReflectionBuffer, m_PreviousReflectionBuffer);
		std::swap(m_ColorBuffer, m_PreviousColorBuffer);
		std::swap(m_ColorDeltaBuffer, m_PreviousColorDeltaBuffer);

		m_FrameIndex++;
	}
}
