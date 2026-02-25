#pragma once

#include <PCH.h>

#include "RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

class RaytracingPass : public RenderPass
{
	nvrhi::ShaderLibraryHandle m_ShaderLibrary;
	nvrhi::rt::PipelineHandle m_RayPipeline;
	nvrhi::rt::ShaderTableHandle m_ShaderTable;
	nvrhi::ShaderHandle m_ComputeShader;
	nvrhi::ComputePipelineHandle m_ComputePipeline;

	nvrhi::BindingLayoutHandle m_BindingLayout;
	nvrhi::BindingSetHandle m_BindingSet;

	nvrhi::rt::AccelStructHandle m_TopLevelAS;

	nvrhi::SamplerHandle m_LinearWrapSampler;

	eastl::vector<nvrhi::rt::InstanceDesc> m_InstanceDescs;

	uint32_t m_TopLevelInstances = 0;

	bool m_DirtyBindings = true;

	/*ResourceHandle m_DirectInput;
	ResourceHandle m_DiffuseOutput;
	ResourceHandle m_SpecularOutput;*/

public:
	RaytracingPass(Renderer* renderer);

	virtual void CreatePipeline() override;

	virtual void ResolutionChanged(uint2 resolution) override;

	void CreateRootSignature();

	bool CreateRayTracingPipeline();

	bool CreateComputePipeline();

	void UpdateAccelStructs(nvrhi::ICommandList* commandList);

	void CheckBindings();

	virtual void Execute(nvrhi::ICommandList* commandList) override;
};