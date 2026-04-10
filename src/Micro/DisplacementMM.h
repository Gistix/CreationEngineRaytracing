#pragma once

#include <PCH.h>

#include "Micro/DisplacementMM.hlsli"
#include "Core/Mesh.h"

struct DisplacementMM
{
	static constexpr uint PACKED_STRIDE = 64;
	struct Pipeline
	{
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		void Create(nvrhi::BindingLayoutHandle bindingLayout, int index);
	};

	Pipeline m_Generate;
	Pipeline m_MinMax;
	Pipeline m_Pack;

	nvrhi::BindingLayoutHandle m_BindingLayout;
	nvrhi::BindingSetHandle m_BindingSet;

	nvrhi::SamplerHandle m_LinearWrapSampler;

	eastl::unique_ptr<DisplacementMMData> m_DMMData;
	nvrhi::BufferHandle m_DMMBuffer;

	uint m_MicroValuesElements;
	nvrhi::BufferHandle m_MicroValuesBuffer;

	uint m_BiasScaleElements;
	nvrhi::BufferHandle m_BiasScaleBuffer;

	void Initialize();

	nvrhi::IBuffer* GetMicroValuesBuffer(uint elements);
	nvrhi::IBuffer* GetBiasScaleBuffer(uint elements);

	void ProcessMesh(nvrhi::ICommandList* commandList, Mesh* mesh);
};