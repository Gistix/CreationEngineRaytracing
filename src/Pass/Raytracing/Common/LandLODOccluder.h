#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Interop/LandLODUpdate.hlsli"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Types/ShaderDefine.h"

namespace Pass
{
	class LandLODOccluder : public RenderPass
	{
		static constexpr uint MAX_MESHES = 256;

		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		bool m_DirtyBindings = true;

		nvrhi::BufferHandle m_Buffer;

		eastl::array<LandLODUpdate, MAX_MESHES> m_VertexUpdateData;
	public:
		LandLODOccluder(Renderer* renderer);

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		bool PrepareResources(nvrhi::ICommandList* commandList, uint32_t& count, uint32_t& vertexCount);

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}