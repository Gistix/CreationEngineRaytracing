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

	for (auto& child : m_Children)
	{
		child.ResolutionChanged(resolution);
	}
}

void RenderNode::SettingsChanged(const Settings& settings)
{
	if (!m_Enabled)
		return;

	if (m_RenderPass)
		m_RenderPass->SettingsChanged(settings);

	for (auto& child : m_Children)
	{
		child.SettingsChanged(settings);
	}
}

void RenderNode::Execute(nvrhi::ICommandList* commandList)
{
	if (!m_Enabled)
		return;

	if (m_RenderPass) {
		commandList->beginMarker(std::format("{} - Pass", m_Name.c_str()).c_str());
		m_RenderPass->Execute(commandList);
		commandList->endMarker();
	}

	for (auto& child : m_Children)
	{
		child.Execute(commandList);
	}
}