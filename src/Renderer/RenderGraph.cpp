#include "RenderGraph.h"
#include "Renderer.h"

RenderGraph::RenderGraph(Renderer* renderer)
{
	m_Renderer = renderer;

	m_RootNode = eastl::make_unique<RootRenderNode>();
}

void RenderGraph::ResolutionChanged(uint2 resolution)
{
	if (m_RootNode)
		m_RootNode->ResolutionChanged(resolution);
}

void RenderGraph::SettingsChanged(const Settings& settings)
{
	if (m_RootNode)
		m_RootNode->SettingsChanged(settings);
}

void RenderGraph::Execute(nvrhi::ICommandList* commandList)
{
	if (m_RootNode)
		m_RootNode->Execute(commandList);
}
