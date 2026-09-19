#include "InstanceLightCulling.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"

namespace Pass
{
	namespace
	{
		struct PushConstants
		{
			uint32_t NumInstances;
			uint32_t NumLights;
			uint32_t ListCapacity;
			uint32_t Pad;
		};

		constexpr uint32_t kThreadGroupSize = 64;
	}

	InstanceLightCulling::InstanceLightCulling(Renderer* renderer)
		: RenderPass(renderer)
	{
	}

	void InstanceLightCulling::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void InstanceLightCulling::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PushConstants)),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0), // Lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1), // InstanceBounds
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // Instances
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1), // InstanceLightList
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)  // LightCounter
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void InstanceLightCulling::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/InstanceLightCulling.hlsl", {}, ShaderStage::Compute);
		if (!shaderBlob)
			return;

		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "Instance Light Culling", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
		if (!m_ComputeShader)
			return;

		m_ComputePipeline = device->createComputePipeline(
			nvrhi::ComputePipelineDesc().setComputeShader(m_ComputeShader).addBindingLayout(m_BindingLayout));
	}

	void InstanceLightCulling::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(PushConstants)),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetInstanceBoundBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, sceneGraph->GetInstanceLightList()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, sceneGraph->GetInstanceLightCounter())
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void InstanceLightCulling::Execute(nvrhi::ICommandList* commandList)
	{
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();

		// Per-instance lists are ignored by the shaders when all lights are evaluated globally.
		if (scene->m_Settings.ExperimentalSettings.GlobalLights)
			return;

		const uint32_t numInstances = sceneGraph->GetNumInstancesFrame();
		if (numInstances == 0 || !m_ComputePipeline)
			return;

		CheckBindings();

		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		const PushConstants pc = {
			numInstances,
			std::min(static_cast<uint32_t>(sceneGraph->GetLights().size()), Constants::LIGHTS_MAX),
			Constants::INSTANCE_LIGHT_LIST_MAX,
			0
		};
		commandList->setPushConstants(&pc, sizeof(pc));

		// Reset the atomic write cursor before instances reserve ranges.
		commandList->clearBufferUInt(sceneGraph->GetInstanceLightCounter(), 0);

		const uint32_t threadGroups = Util::Math::DivideRoundUp(numInstances, kThreadGroupSize);
		commandList->dispatch(threadGroups, 1, 1);
	}
}
