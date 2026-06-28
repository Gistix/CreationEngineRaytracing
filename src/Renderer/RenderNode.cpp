#include "RenderNode.h"
#include "Scene.h"
#include "Renderer.h"

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
		auto& debugSettings = Scene::GetSingleton()->m_Settings.DebugSettings;

		if (debugSettings.Markers)
			commandList->beginMarker(std::format("{} - Pass", m_Name.c_str()).c_str());

		auto currentSlot = Renderer::GetSingleton()->GetCurrentSlot();

		if (!m_TimerQueries[currentSlot])
			m_TimerQueries[currentSlot] = Renderer::GetSingleton()->GetDevice()->createTimerQuery();

		if (debugSettings.Timings)
			commandList->beginTimerQuery(m_TimerQueries[currentSlot]);

		m_RenderPass->Execute(commandList);

		if (debugSettings.Timings)
			commandList->endTimerQuery(m_TimerQueries[currentSlot]);

		if (debugSettings.Markers)
			commandList->endMarker();
	}

	for (auto& child : m_Children)
	{
		child.Execute(commandList);
	}
}
