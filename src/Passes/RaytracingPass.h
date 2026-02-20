#pragma once

#include <PCH.h>

#include "RenderPass.h"
#include "FrameData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

struct RaytracingPass : IRenderPass
{
	nvrhi::ShaderLibraryHandle m_ShaderLibrary;
	nvrhi::rt::PipelineHandle m_RayPipeline;
	nvrhi::rt::ShaderTableHandle m_ShaderTable;
	nvrhi::ShaderHandle m_ComputeShader;
	nvrhi::ComputePipelineHandle m_ComputePipeline;

	nvrhi::BindingLayoutHandle m_BindingLayout;
	nvrhi::BindingSetHandle m_BindingSet;
	nvrhi::BindingLayoutHandle m_BindlessLayout;

	nvrhi::rt::AccelStructHandle m_TopLevelAS;

	nvrhi::BufferHandle m_ConstantBuffer;

	eastl::shared_ptr<DescriptorTableManager> m_DescriptorTable;

	eastl::unique_ptr<FrameData> m_FrameData;

	nvrhi::SamplerHandle m_LinearWrapSampler;

	eastl::vector<nvrhi::rt::InstanceDesc> instances;

	uint32_t m_TopLevelInstances = 0;

	bool m_DirtyBindings = true;

	virtual void Init() override;

	virtual void CreatePipeline() override;

	virtual void ResolutionChanged(uint2 resolution) override;

	void UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const;

	void CreateRootSignature();

	bool CreateRayTracingPipeline();

	bool CreateComputePipeline();

	void UpdateAccelStructs(nvrhi::ICommandList* commandList);

	void CheckBindings();

	virtual void Execute(nvrhi::ICommandList* commandList) override;
};