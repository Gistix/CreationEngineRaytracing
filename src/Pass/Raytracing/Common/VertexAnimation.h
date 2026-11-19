#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Interop/VertexAnimation.hlsli"
#include "Types/RingBuffer.h"

class BaseMesh;

namespace Pass
{
	// Model-space vertex animation (TREE_ANIM sway, BSDynamicTriShape morphs, grass wind). Runs before Skinning:
	// skinned meshes get pre-skin positions in their animated position buffer (Skinning consumes them),
	// other meshes get their live vertices and previous positions written directly. Merged grass groups
	// get per-instance swayed positions (current + previous) in their dynamic position buffer.
	class VertexAnimation : public RenderPass
	{
		static constexpr uint MAX_MESHES = 4096;
		static constexpr uint MAX_GRASS_GROUPS = 4096;

		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::ShaderHandle m_GrassComputeShader;
		nvrhi::ComputePipelineHandle m_GrassComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;

		RingBuffer m_Buffer;
		RingBuffer m_GrassBuffer;

		eastl::array<VertexAnimationData, MAX_MESHES> m_AnimationData;
		eastl::array<GrassWindData, MAX_GRASS_GROUPS> m_GrassData;

		// Mesh updates run on the thread pool, so queueing is mutex-protected.
		eastl::unordered_map<BaseMesh*, VertexAnimationData> m_QueuedMeshes;
		eastl::unordered_map<BaseMesh*, GrassWindData> m_QueuedGrass;
		std::mutex m_QueueMutex;

		bool PrepareMeshes(nvrhi::ICommandList* commandList, uint32_t& numMeshes, uint32_t& numVertices);
		bool PrepareGrass(nvrhi::ICommandList* commandList, uint32_t& numGroups, uint32_t& numVertices);

	public:
		VertexAnimation(Renderer* renderer);

		virtual void Initialize() override;
		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void QueueUpdate(BaseMesh* mesh, const VertexAnimationData& data);
		void QueueGrassUpdate(BaseMesh* mesh, const GrassWindData& data);

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
