#include "RenderGraph.h"
#include "Renderer.h"

RenderGraph::RenderGraph(Renderer* renderer)
{
	m_Renderer = renderer;
}

void RenderGraph::ResolutionChanged(uint2 resolution)
{
	for (auto& node : m_Nodes)
	{
		node.ResolutionChanged(resolution);
	}
}

void RenderGraph::SettingsChanged(const Settings& settings)
{
	for (auto& node : m_Nodes)
	{
		node.SettingsChanged(settings);
		if (node.IsActive() && node.m_RenderPass)
		{
			node.m_RenderPass->EnsureInitialized();
		}
	}
}

void RenderGraph::Execute(nvrhi::ICommandList* commandList)
{
	for (auto& node : m_Nodes)
	{
		node.Execute(commandList);
	}
}
