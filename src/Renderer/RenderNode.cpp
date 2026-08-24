#include "RenderNode.h"
#include "Scene.h"
#include "Renderer.h"

#include <chrono>

void RenderNode::ResolutionChanged(uint2 resolution)
{
	if (!m_Enabled)
		return;

	if (m_RenderPass)
		m_RenderPass->ResolutionChanged(resolution);
}

void RenderNode::SettingsChanged(const Settings& settings)
{
	if (m_RenderPass)
		m_RenderPass->SettingsChanged(settings);
}

void RenderNode::Execute(nvrhi::ICommandList* commandList)
{
	auto currentSlot = Renderer::GetSingleton()->GetCurrentSlot();

	if (!IsActive()) {
		m_ExecutedThisFrame[currentSlot] = false;
		return;
	}

	m_ExecutedThisFrame[currentSlot] = true;

	if (m_RenderPass) {
		m_RenderPass->EnsureInitialized();

		auto& debugSettings = Scene::GetSingleton()->m_Settings.DebugSettings;

		if (debugSettings.Markers)
			commandList->beginMarker(std::format("{} - Pass", m_Name.c_str()).c_str());

		if (debugSettings.Timings != TimingMode::Disabled) {
			if (!m_TimerQueries[currentSlot])
				m_TimerQueries[currentSlot] = Renderer::GetSingleton()->GetDevice()->createTimerQuery();

			commandList->beginTimerQuery(m_TimerQueries[currentSlot]);

			auto cpuStart = std::chrono::high_resolution_clock::now();

			m_RenderPass->Execute(commandList);

			auto cpuEnd = std::chrono::high_resolution_clock::now();

			commandList->endTimerQuery(m_TimerQueries[currentSlot]);

			m_CpuTimes[currentSlot] = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
		} else {
			m_RenderPass->Execute(commandList);
		}

		if (debugSettings.Markers)
			commandList->endMarker();
	}
}
