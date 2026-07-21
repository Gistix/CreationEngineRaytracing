#include "TransformComposition.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"

namespace Pass
{
	TransformComposition::TransformComposition(Renderer* renderer)
		: RenderPass(renderer)
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void TransformComposition::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			nvrhi::BindingLayoutItem::PushConstants(0, sizeof(uint32_t)),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0), // MeshesData
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1), // InstancesData
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2), // CurrentTransforms
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3), // PrevTransforms
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)  // TransformsOut
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void TransformComposition::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/TransformComposition.hlsl", {}, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void TransformComposition::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto& transformManager = sceneGraph->GetTransformManager();

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(uint32_t)),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, transformManager->GetCurrentBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, transformManager->GetPrevBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, transformManager->GetBuffer())
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void TransformComposition::Execute(nvrhi::ICommandList* commandList)
	{
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();

		const uint32_t numMeshes = sceneGraph->GetNumMeshesFrame();
		if (numMeshes == 0)
			return;

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		const uint32_t pushConstants = numMeshes;
		commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

		const uint32_t threadGroups = Util::Math::DivideRoundUp(numMeshes, 64u);
		commandList->dispatch(threadGroups, 1, 1);
	}
}
