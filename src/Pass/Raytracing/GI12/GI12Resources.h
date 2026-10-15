#pragma once

#include <PCH.h>
#include <nvrhi/nvrhi.h>

#include "interop/GI12Data.hlsli"
#include "interop/GI12Types.h"
#include "Types/Settings.h"
#include "Constants.h"

class Renderer;

namespace Pass::Raytracing::GI12
{
	class GI12Resources
	{
	public:
		Renderer* m_Renderer = nullptr;
		uint2 m_Resolution = { 0, 0 };
		uint32_t m_FrameIndex = 0;

		// -------------------------------------------------------------
		// Common Samplers
		// -------------------------------------------------------------
		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;
		nvrhi::SamplerHandle m_PointClampSampler;

		// -------------------------------------------------------------
		// Constant Buffers (Volatile)
		// -------------------------------------------------------------
		nvrhi::BufferHandle m_GI12CB;
		nvrhi::BufferHandle m_ScreenProbesCB;
		nvrhi::BufferHandle m_HashGridCacheCB;
		nvrhi::BufferHandle m_WorldSpaceReSTIRCB;
		nvrhi::BufferHandle m_GlossyReflectionsCB;
		nvrhi::BufferHandle m_GlossyAtrousCB;
		nvrhi::BufferHandle m_GIDenoiserCB;

		// -------------------------------------------------------------
		// Screen Probes Resources
		// -------------------------------------------------------------
		uint2 m_ProbeCount = { 0, 0 };
		nvrhi::TextureHandle m_ProbeMaskBuffer;
		nvrhi::TextureHandle m_PreviousProbeMaskBuffer;
		nvrhi::TextureHandle m_ProbeBuffer;
		nvrhi::TextureHandle m_PreviousProbeBuffer;
		nvrhi::BufferHandle  m_ProbeSHBuffer;
		nvrhi::BufferHandle  m_PreviousProbeSHBuffer;
		nvrhi::BufferHandle  m_ProbeSpawnBuffer;
		nvrhi::BufferHandle  m_ProbeSpawnSampleBuffer;
		nvrhi::BufferHandle  m_ProbeSpawnRadianceBuffer;
		nvrhi::BufferHandle  m_EmptyTileBuffer;
		nvrhi::BufferHandle  m_EmptyTileCountBuffer;
		nvrhi::BufferHandle  m_ProbeCachedTileLRUBuffer;

		// -------------------------------------------------------------
		// Hash Grid Cache Resources
		// -------------------------------------------------------------
		uint32_t m_NumTiles = 0;
		uint32_t m_NumCells = 0;
		nvrhi::BufferHandle m_HashBuffer;
		nvrhi::BufferHandle m_DecayTileBuffer;
		nvrhi::BufferHandle m_CellBuffer;
		nvrhi::BufferHandle m_CellPackedBuffer;
		nvrhi::BufferHandle m_MultibounceCellBuffer;
		nvrhi::BufferHandle m_UpdateTileBuffer;
		nvrhi::BufferHandle m_UpdateTileCountBuffer;
		nvrhi::BufferHandle m_VisibilityBuffer;
		nvrhi::BufferHandle m_VisibilityCountBuffer0;
		nvrhi::BufferHandle m_VisibilityCountBuffer1;
		nvrhi::BufferHandle m_VisibilityCellBuffer;
		nvrhi::BufferHandle m_MultibounceInfoBuffer;

		// -------------------------------------------------------------
		// World Space ReSTIR Resources
		// -------------------------------------------------------------
		nvrhi::BufferHandle m_ReservoirBuffer;
		nvrhi::BufferHandle m_MultibounceReservoirBuffer;

		// -------------------------------------------------------------
		// Glossy Reflections Resources
		// -------------------------------------------------------------
		nvrhi::BufferHandle  m_RtSampleBuffer;
		nvrhi::BufferHandle  m_RtSampleCountBuffer;
		nvrhi::TextureHandle m_SpecularBuffer;
		nvrhi::TextureHandle m_DirectionBuffer;
		nvrhi::TextureHandle m_ReflectionsBuffer0;
		nvrhi::TextureHandle m_AverageSquaredBuffer0;
		nvrhi::TextureHandle m_ReflectionsBuffer1;
		nvrhi::TextureHandle m_AverageSquaredBuffer1;
		nvrhi::TextureHandle m_FirefliesBuffer;
		nvrhi::TextureHandle m_StandardDevBuffer;
		nvrhi::TextureHandle m_ReflectionBuffer;
		nvrhi::TextureHandle m_PreviousReflectionBuffer;

		// -------------------------------------------------------------
		// GI Denoiser Resources
		// -------------------------------------------------------------
		nvrhi::TextureHandle m_BlurMask;
		nvrhi::TextureHandle m_ColorBuffer;
		nvrhi::TextureHandle m_ColorDeltaBuffer;
		nvrhi::TextureHandle m_PreviousColorBuffer;
		nvrhi::TextureHandle m_PreviousColorDeltaBuffer;
		nvrhi::TextureHandle m_IrradianceBuffer;

		// Direct3D / Matrix tracking
		DirectX::XMMATRIX m_PrevViewProj = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX m_PrevViewProjInv = DirectX::XMMatrixIdentity();
		DirectX::XMFLOAT3 m_PrevEye = { 0.0f, 0.0f, 0.0f };

	public:
		GI12Resources(Renderer* renderer);
		~GI12Resources() = default;

		void Initialize();
		void ResolutionChanged(uint2 resolution);
		void UpdateConstants(nvrhi::ICommandList* commandList, const Settings& settings);
		void SwapHistory();
	};
}
