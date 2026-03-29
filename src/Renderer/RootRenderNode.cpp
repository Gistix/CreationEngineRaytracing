#include "RootRenderNode.h"
#include "Renderer/FrameGraphBuilder.h"

void RootRenderNode::AttachRenderNode(RenderNode* renderNode)
{
	auto it = eastl::find(m_Children.begin(), m_Children.end(), renderNode);

	if (it != m_Children.end()) {
		logger::warn("RootRenderNode::AttachRenderNode - Node already attached");
		return;
	}

	m_Children.push_back(renderNode);
}

void RootRenderNode::DetachRenderNode(RenderNode* renderNode)
{
	auto newEnd = eastl::remove(
		m_Children.begin(),
		m_Children.end(),
		renderNode
	);

	m_Children.erase(newEnd, m_Children.end());
}

void RootRenderNode::Setup(FrameGraphBuilder& builder, const Settings& settings)
{
	if (!m_Enabled)
		return;

	for (auto* child : m_Children)
	{
		child->Setup(builder, settings);
	}
}

void RootRenderNode::ResolutionChanged(uint2 resolution)
{
	if (!m_Enabled)
		return;

	for (auto* child : m_Children)
	{
		child->ResolutionChanged(resolution);
	}
}

void RootRenderNode::SettingsChanged(const Settings& settings)
{
	if (!m_Enabled)
		return;

	for (auto* child : m_Children)
	{
		child->SettingsChanged(settings);
	}
}

void RootRenderNode::Execute(nvrhi::ICommandList* commandList)
{
	if (!m_Enabled)
		return;

	for (auto* child : m_Children)
	{
		child->Execute(commandList);
	}
}
