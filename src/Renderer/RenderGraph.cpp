#include "RenderGraph.h"
#include "Renderer.h"

RenderGraph::RenderGraph(Renderer* renderer)
{
	m_Renderer = renderer;
}

void RenderGraph::AttachRootNode(RenderNode* rootNode)
{
	m_RootNode = rootNode;
}

void RenderGraph::DetachRootNode()
{
	m_RootNode = nullptr;
}

void RenderGraph::ResolutionChanged(uint2 resolution)
{
	if (m_RootNode)
		m_RootNode->ResolutionChanged(resolution);
}

void RenderGraph::Execute(nvrhi::ICommandList* commandList)
{
	if (m_RootNode)
		m_RootNode->Execute(commandList);
}