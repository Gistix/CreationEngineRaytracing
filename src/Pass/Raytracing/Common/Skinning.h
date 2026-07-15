#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Interop/VertexUpdate.hlsli"
#include "Interop/BoneMatrix.hlsli"
#include "Interop/BoneTransform.hlsli"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"
#include "Types/RingBuffer.h"

#include "Types/ShaderDefine.h"
#include "Types/RingBuffer.h"

class SkinnedMesh;

namespace Pass
{
	class Skinning : public RenderPass
	{
		static constexpr uint MAX_GEOMETRY = 2048;
		static constexpr uint MAX_BONE_MATRICES = MAX_GEOMETRY * 10;

		// ---- Bone Compute Pass ----
		nvrhi::ShaderLibraryHandle m_BoneShaderLibrary;
		nvrhi::ShaderHandle m_BoneComputeShader;
		nvrhi::ComputePipelineHandle m_BoneComputePipeline;
		nvrhi::BindingLayoutHandle m_BoneBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BoneBindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BoneBindingSetDirty {};

		RingBuffer m_BoneWorldBuffer;
		RingBuffer m_SkinToBoneBuffer;
		RingBuffer m_MeshBoneHeaderBuffer;

		eastl::array<NiTransformPacked, MAX_BONE_MATRICES> m_BoneWorldData;
		eastl::array<NiTransformPacked, MAX_BONE_MATRICES> m_SkinToBoneData;
		eastl::array<MeshBoneHeader, MAX_GEOMETRY> m_MeshBoneHeaderData;

		// ---- Vertex Skinning Pass ----
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

		RingBuffer m_VertexUpdateBuffer;
		RingBuffer m_BoneMatrixBuffer;

		eastl::array<VertexUpdateData, MAX_GEOMETRY> m_VertexUpdateData;
		eastl::array<BoneMatrix, MAX_BONE_MATRICES> m_BoneMatrixData;

		struct QueuedMesh
		{
			DirtyFlags updateFlags;
			eastl::string path;
		};

		eastl::unordered_map<SkinnedMesh*, QueuedMesh> queuedMeshes;
		std::mutex m_QueueMutex;

	public:
		Skinning(Renderer* renderer);

		void CreateBoneBindingLayout();
		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void QueueUpdate(DirtyFlags updateFlags, SkinnedMesh* mesh);
		bool PrepareResources(nvrhi::ICommandList* commandList, uint32_t& count, uint32_t& vertexCount);
		void ClearQueue();

		void CheckBoneBindings();
		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
