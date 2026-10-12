#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "GI12Data.hlsli"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"

#include "Events/ITLASUpdateListener.h"

namespace Pass::Raytracing::GI12
{
	// AMD Capsaicin GI-1.2 port: two-level radiance cache.
	// L1: screen probes (sparse SH cache + interpolate).
	// L2: world hash-grid cache (replaces/augments SHaRC when enabled).
	// Glossy: half-res reflection trace + atrous denoise (GI-1.1).
	// Disabled by default; side-by-side with SHaRCGI/GlobalIllumination.
	class GI12Pass : public RenderPass, ITLASUpdateListener
	{
		static constexpr uint32_t MAX_BUCKETS_LOG2 = 14;
		static constexpr uint32_t MAX_TILES_PER_BUCKET_LOG2 = 4;

		nvrhi::BufferHandle m_ConstantBuffer;
		eastl::unique_ptr<GI12Data> m_ConstantData;

		nvrhi::BufferHandle m_ScreenProbesDataBuffer;
		eastl::unique_ptr<GI12ScreenProbesData> m_ScreenProbesData;

		nvrhi::BufferHandle m_HashGridDataBuffer;
		eastl::unique_ptr<GI12HashGridData> m_HashGridData;

		// L1 screen-probe history: world position + SH coefficients.
		nvrhi::BufferHandle m_ProbePositionBuffer;
		nvrhi::BufferHandle m_ProbeNormalBuffer;
		nvrhi::BufferHandle m_ProbeLastUpdatedBuffer;
		nvrhi::BufferHandle m_ProbeSHBuffer;
		nvrhi::BufferHandle m_ProbeRadianceBuffer;
		nvrhi::BufferHandle m_PreviousProbePositionBuffer;
		nvrhi::BufferHandle m_PreviousProbeNormalBuffer;
		nvrhi::BufferHandle m_PreviousProbeSHBuffer;
		nvrhi::BufferHandle m_PreviousProbeRadianceBuffer;
		uint32_t m_MaxProbes = 0;
		uint32_t m_ProbeSize = 0;

		// L2 hash-grid cache: Capsaicin layout (gi1_shared.h, MIT).
		// Hash = transient per-frame tile hash (uint, 0 = empty), cleared each
		// frame; DecayTile = last-touched frame for PurgeTiles (k=50).
		// Value/ValueIndirect = persistent half-packed radiance (uint2 packHalf4).
		// UpdateCellValue* = per-frame quantized scratch (1e3) merged by UpdateTiles.
		nvrhi::BufferHandle m_HashBuffer;
		nvrhi::BufferHandle m_DecayTileBuffer;
		nvrhi::BufferHandle m_ValueBuffer;
		nvrhi::BufferHandle m_ValueIndirectBuffer;
		nvrhi::BufferHandle m_UpdateCellValueBuffer;
		nvrhi::BufferHandle m_UpdateCellValueIndirectBuffer;
		// Stable previous-frame snapshot for multibounce reads during fill.
		nvrhi::BufferHandle m_PreviousHashBuffer;
		nvrhi::BufferHandle m_PreviousValueBuffer;
		nvrhi::BufferHandle m_PreviousValueIndirectBuffer;
		nvrhi::BufferHandle m_TileUpdateBuffer;
		nvrhi::BufferHandle m_TileUpdateCountBuffer;
		uint32_t m_NumBuckets = 0;
		uint32_t m_NumTiles = 0;
		uint32_t m_NumCells = 0;

		nvrhi::ShaderHandle m_ClearShader;
		nvrhi::ComputePipelineHandle m_ClearPipeline;
		nvrhi::ShaderHandle m_UpdateShader;
		nvrhi::ComputePipelineHandle m_UpdatePipeline;
		nvrhi::ShaderHandle m_UpdateTilesShader;
		nvrhi::ComputePipelineHandle m_UpdateTilesPipeline;
		nvrhi::ShaderHandle m_ResolveShader;
		nvrhi::ComputePipelineHandle m_ResolvePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};

		// Separate layout for the per-pixel resolve: GBuffer SRVs + cache
		// SRVs + radiance UAVs (avoids UAV register overlap with u0-u7).
		nvrhi::BindingLayoutHandle m_ResolveLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_ResolveSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_ResolveDirty{};

		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;

		// True when the resolve layout carries the specular UAV (NRD path).
		bool m_ResolveSpecular = false;

		SceneTLAS* m_SceneTLAS;
		eastl::vector<ShaderDefine> m_Defines;

		uint32_t m_FrameIndex = 0;
		uint64_t m_LightingRevision = UINT64_MAX;
		bool m_ResetCache = true;

		void CreateBuffers();
		void ClearCache(nvrhi::ICommandList* commandList);
		void CreateBindingLayout();
		void CheckBindings();
		void CreateResolveLayout(bool specular);
		void CheckResolveBindings();
		void UpdateConstants();

	public:
		GI12Pass(Renderer* renderer, SceneTLAS* sceneTLAS);

		void SceneTexturesChanged() override
		{
			m_BindingSetDirty.fill(true);
			m_ResolveDirty.fill(true);
		}

		auto GetValueBuffer() { return m_ValueBuffer; }
		auto GetValueIndirectBuffer() { return m_ValueIndirectBuffer; }
		auto GetHashBuffer() { return m_HashBuffer; }
		auto GetHashGridDataBuffer() { return m_HashGridDataBuffer; }

		// True when the pass can produce radiance (pipelines compiled).
		// GlobalIllumination uses this to stand down in favor of the resolve.
		bool IsResolveActive() const { return m_Enabled && m_ResolvePipeline; }

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_BindingSetDirty.fill(true);
			m_ResolveDirty.fill(true);
		}

		void Initialize() override;
		void CreatePipeline() override;
		void ResolutionChanged(uint2 resolution) override;
		void SettingsChanged(const Settings& settings) override;
		void Execute(nvrhi::ICommandList* commandList) override;
	};
}
