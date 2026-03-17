#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Interop/VertexUpdate.hlsli"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Types/ShaderDefine.h"

namespace Pass
{
	class Skinning : public RenderPass
	{
		static constexpr uint MAX_GEOMETRY = 2048;
		static constexpr uint MAX_BONE_MATRICES = MAX_GEOMETRY * 10;

		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		bool m_DirtyBindings = true;

		nvrhi::BufferHandle m_VertexUpdateBuffer;
		nvrhi::BufferHandle m_BoneMatrixBuffer;

		eastl::array<VertexUpdateData, MAX_GEOMETRY> m_VertexUpdateData;
		eastl::array<float3x4, MAX_BONE_MATRICES> m_BoneMatrixData;

		struct QueuedMesh
		{
			DirtyFlags updateFlags;
			eastl::string path;
		};

		eastl::unordered_map<Mesh*, QueuedMesh> queuedMeshes;

	public:
		Skinning(Renderer* renderer);

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void QueueUpdate(DirtyFlags updateFlags, Mesh* mesh);
		bool PrepareResources(nvrhi::ICommandList* commandList, uint32_t& count, uint32_t& vertexCount);
		void ClearQueue();

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}