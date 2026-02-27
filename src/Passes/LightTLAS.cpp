#include "LightTLAS.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	LightTLAS::LightTLAS(Renderer* renderer)
		: RenderPass(renderer)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_LIGHTS_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(4).setSize(UINT_MAX)
		};

		auto device = renderer->GetDevice();

		m_BindlessLayout = device->createBindlessLayout(bindlessLayoutDesc);
		m_DescriptorTable = device->createDescriptorTable(m_BindlessLayout);

		if (m_DescriptorTable->getCapacity() < Constants::NUM_LIGHTS_MAX)
		{
			device->resizeDescriptorTable(m_DescriptorTable, Constants::NUM_LIGHTS_MAX);
		}
	}

	void LightTLAS::Execute(nvrhi::ICommandList* commandList)
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		uint8_t lightIndex = 0;

		for (auto& light : sceneGraph->GetLights())
		{
			light.UpdateTLAS(commandList);

			if (light.m_DirtyBinding || light.m_Index)
			{
				light.m_Index = lightIndex;

				auto bindingSet = nvrhi::BindingSetItem::RayTracingAccelStruct(lightIndex, light.m_TopLevelAS);
				Renderer::GetSingleton()->GetDevice()->writeDescriptorTable(m_DescriptorTable, bindingSet);
			}

			lightIndex++;
		}
	}
}