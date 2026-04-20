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
		void Allocate(DescriptorIndex descriptorIndex, ID3D11Texture2D* texture);

	private:
		nvrhi::ShaderHandle m_MSNVertexShader;
		nvrhi::ShaderHandle m_MSNPixelShader;
		nvrhi::BindingLayoutHandle m_MSNBindingLayout;
		nvrhi::SamplerHandle m_MSNSampler;
		nvrhi::GraphicsPipelineHandle m_MSNGraphicsPipeline;

		eastl::unordered_map<DescriptorIndex, ID3D11Texture2D*> m_MSNAllocationMap;
	};
};