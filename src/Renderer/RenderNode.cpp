#include "RenderNode.h"
#include "Renderer/FrameGraphBuilder.h"
#include "Renderer.h"

void RenderNode::AddNode(RenderNode renderNode)
{
	m_Children.push_back(eastl::move(renderNode));
}

void RenderNode::Setup(FrameGraphBuilder& builder, const Settings& settings)
{
	if (!m_Enabled)
		return;

	if (m_RenderPass)
	{
		builder.BeginPass();
		m_RenderPass->Setup(builder, settings);
	}

	for (auto& child : m_Children)
	{
		child.Setup(builder, settings);
	}
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
		Renderer::GetSingleton()->GetResourceManager().BeginPass();
		commandList->beginMarker(std::format("{} - Pass", m_Name.c_str()).c_str());
		m_RenderPass->Execute(commandList);
		commandList->endMarker();
	}

	for (auto& child : m_Children)
	{
		child.Execute(commandList);
	}
}
