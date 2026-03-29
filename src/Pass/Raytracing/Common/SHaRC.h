#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/LightTLAS.h"

#include "Interop/SHaRCData.hlsli"

#include "Events/ITLASUpdateListener.h"

namespace Pass
{
	class SHaRC : public RenderPass, ITLASUpdateListener
	{		
		static constexpr uint UPDATE_THREAD_GROUP_SIZE = 16;
		static constexpr uint RESOLVE_LINEAR_BLOCK_SIZE = 256;
		static constexpr size_t MAX_CAPACITY = 4 * 1024 * 1024;

		eastl::unique_ptr<SHaRCData> m_SHaRCData;
		nvrhi::BufferHandle m_SHaRCBuffer;

		struct SubPass {
			nvrhi::ShaderHandle m_ComputeShader;
			nvrhi::ComputePipelineHandle m_ComputePipeline;

			nvrhi::BindingLayoutHandle m_BindingLayout;
			nvrhi::BindingSetHandle m_BindingSet;
		};

		SubPass m_UpdatePass;
		SubPass m_ResolvePass;

		nvrhi::SamplerHandle m_LinearWrapSampler;

		nvrhi::BufferHandle m_HashEntriesBuffer;
		nvrhi::BufferHandle m_LockBuffer;
		nvrhi::BufferHandle m_AccumulationBuffer;
		nvrhi::BufferHandle m_ResolveBuffer;

		SceneTLAS* m_SceneTLAS;

		eastl::vector<ShaderDefine> m_Defines;

		bool m_Enabled = true;
		bool m_DirtyBindings = true;
	public:
		SHaRC(Renderer* renderer, SceneTLAS* sceneTLAS);

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_DirtyBindings = true;
		}

		auto GetSHaRCConstantBuffer() { return m_SHaRCBuffer; }
		auto GetHashEntriesBuffer() { return m_HashEntriesBuffer; }
		auto GetResolveBuffer() { return m_ResolveBuffer; }

		void SetupUpdate();

		void SetupResolve();

		void CheckBindings();

		virtual void SettingsChanged(const Settings& settings) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}