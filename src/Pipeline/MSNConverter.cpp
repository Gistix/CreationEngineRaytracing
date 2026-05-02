#include "MSNConverter.h"
#include "Core/Model.h"
#include "ShaderUtils.h"
#include "Renderer.h"
#include "Constants.h"
#include "SceneGraph.h"

namespace Pipeline
{
	MSNConverter::MSNConverter()
	{
		auto device = Renderer::GetSingleton()->GetDevice();

		// Compile shaders
		winrt::com_ptr<IDxcBlob> vertexBlob, pixelBlob;
		ShaderUtils::CompileShader(vertexBlob, L"data/shaders/ModelSpaceToTangent.hlsl", {}, L"vs_6_5", L"MainVS");
		ShaderUtils::CompileShader(pixelBlob, L"data/shaders/ModelSpaceToTangent.hlsl", {}, L"ps_6_5", L"MainPS");

		m_VertexShader = device->createShader({ nvrhi::ShaderType::Vertex, "", "MainVS" }, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize());
		m_PixelShader = device->createShader({ nvrhi::ShaderType::Pixel, "", "MainPS" }, pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize());

		// Create sampler
		m_Sampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		// Create binding layout for MSN pass
		auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(uint32_t)))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0));

		m_BindingLayout = device->createBindingLayout(bindingLayoutDesc);
	}

	void MSNConverter::Allocate(DescriptorIndex descriptorIndex, ID3D11Resource* texture)
	{
		m_AllocationMap.emplace(descriptorIndex, texture);
	}

	void MSNConverter::Convert(Model* model, nvrhi::ICommandList* commandList, SceneGraph* sceneGraph)
	{
		auto device = Renderer::GetSingleton()->GetDevice();

		// Group meshes by their converted normal map (descriptor index)
		eastl::unordered_map<DescriptorIndex, eastl::vector<Mesh*>> msnGroups;

		for (auto& mesh : model->meshes) {
			if (mesh->material.shaderFlags.none(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals))
				continue;

			auto descHandle = mesh->material.Textures[Constants::Material::NORMALMAP_TEXTURE].texture.lock();
			if (!descHandle)
				continue;

			auto key = descHandle->Get();
			msnGroups[key].push_back(mesh.get());
		}

		auto& triangleDescriptors = sceneGraph->GetTriangleDescriptors();
		auto& vertexDescriptors = sceneGraph->GetVertexDescriptors();

		auto& normapMaps = sceneGraph->GetTextureManager()->m_NormalMaps;

		for (auto& [allocationIdx, meshes] : msnGroups) {
			auto msnIt = m_AllocationMap.find(allocationIdx);
			if (msnIt == m_AllocationMap.end())
				continue;

			auto normalMapIt = normapMaps.find(msnIt->second);
			if (normalMapIt == normapMaps.end()) {
				m_AllocationMap.erase(msnIt);
				continue;
			}

			auto* normalMap = normalMapIt->second.get();
			if (normalMap->converted) {
				m_AllocationMap.erase(msnIt);
				continue;
			}

			// Create framebuffer for this render target
			auto framebuffer = device->createFramebuffer(
				nvrhi::FramebufferDesc().addColorAttachment(normalMap->texture));

			const auto& fbinfo = framebuffer->getFramebufferInfo();

			// Create pipeline lazily (all converted textures share R10G10B10A2_UNORM)
			if (!m_GraphicsPipeline) {
				nvrhi::GraphicsPipelineDesc pipelineDesc;
				pipelineDesc.VS = m_VertexShader;
				pipelineDesc.PS = m_PixelShader;
				pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
				pipelineDesc.bindingLayouts = {
					m_BindingLayout,
					triangleDescriptors->m_Layout,
					vertexDescriptors->m_Layout
				};
				pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
				pipelineDesc.renderState.rasterState.setCullNone();

				m_GraphicsPipeline = device->createGraphicsPipeline(pipelineDesc, fbinfo);
			}

			// Create binding set with the source MSN texture
			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(uint32_t)),
				nvrhi::BindingSetItem::Texture_SRV(0, normalMap->sourceTexture),
				nvrhi::BindingSetItem::Sampler(0, m_Sampler)
			};
			auto bindingSet = device->createBindingSet(bindingSetDesc, m_BindingLayout);

			// Clear RT to flat normal (0.5, 0.5, 1.0, 1.0)
			commandList->clearTextureFloat(normalMap->texture, nvrhi::AllSubresources, nvrhi::Color(0.5f, 0.5f, 1.0f, 1.0f));

			for (auto* mesh : meshes) {
				uint32_t geometryIdx = mesh->m_DescriptorHandle.Get();

				nvrhi::GraphicsState state;
				state.pipeline = m_GraphicsPipeline;
				state.framebuffer = framebuffer;
				state.bindings = {
					bindingSet,
					triangleDescriptors->m_DescriptorTable->GetDescriptorTable(),
					vertexDescriptors->m_DescriptorTable
				};
				state.viewport.addViewportAndScissorRect(fbinfo.getViewport());

				commandList->setGraphicsState(state);
				commandList->setPushConstants(&geometryIdx, sizeof(geometryIdx));

				nvrhi::DrawArguments args;
				args.vertexCount = mesh->triangleData.count * 3;
				args.instanceCount = 1;
				commandList->draw(args);
			}

			normalMap->converted = true;
			m_AllocationMap.erase(msnIt);
		}
	}
};