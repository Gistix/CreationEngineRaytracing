#include "RenderNode.h"

void RenderNode::AddNode(RenderNode renderNode)
{
	m_Children.push_back(eastl::move(renderNode));
}

void RenderNode::ResolutionChanged(uint2 resolution)
{
	if (!m_Enabled)
		return;

	if (m_RenderPass)
		m_RenderPass->ResolutionChanged(resolution);

	for (auto& m_ChildPass : m_Children)
	{
		m_ChildPass.ResolutionChanged(resolution);
	}
}

void RenderNode::Execute(nvrhi::ICommandList* commandList)
{
	if (!m_Enabled)
		return;

	if (m_RenderPass)
		m_RenderPass->Execute(commandList);

	for (auto& m_ChildPass : m_Children)
	{
		m_ChildPass.Execute(commandList);
	}
}