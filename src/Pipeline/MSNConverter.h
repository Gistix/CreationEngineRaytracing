#pragma once

#include "Framework/DescriptorTableManager.h"

struct Model;
class SceneGraph;

namespace Pipeline
{
	struct MSNConverter
	{
		MSNConverter();
		void Convert(Model* model, nvrhi::ICommandList* commandList, SceneGraph* sceneGraph);
		void Allocate(DescriptorIndex descriptorIndex, ID3D11Resource* texture);

	private:
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::GraphicsPipelineHandle m_GraphicsPipeline;

		eastl::unordered_map<DescriptorIndex, ID3D11Resource*> m_AllocationMap;
	};
};