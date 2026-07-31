#pragma once

#include "Pass/RenderPass.h"
#include "Constants.h"

struct RenderNode
{
	RenderNode(bool enabled, const char* name) :
		m_Enabled(enabled), m_Name(name) {
	}

	RenderNode(bool enabled, const char* name, eastl::unique_ptr<RenderPass> renderPass) :
		m_Enabled(enabled), m_Name(name), m_RenderPass(eastl::move(renderPass)) {
	}

	template<typename T>
	T* GetPass()
	{
		static_assert(eastl::is_base_of_v<RenderPass, T>,
			"T must derive from RenderPass");

		if (!m_RenderPass)
			return nullptr;

		return dynamic_cast<T*>(m_RenderPass.get());
	}

	bool IsActive() const
	{
		return m_Enabled && m_RenderPass && m_RenderPass->IsEnabled();
	}

	void SetEnabled(bool enabled) { 
		m_Enabled = enabled; 
		if (m_RenderPass)
			m_RenderPass->SetEnabled(enabled);
	}

	void ResolutionChanged(uint2 resolution);

	void SettingsChanged(const Settings& settings);

	void Execute(nvrhi::ICommandList* commandList);

	bool m_Enabled = true;
	eastl::string m_Name;
	eastl::unique_ptr<RenderPass> m_RenderPass;
	eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_ExecutedThisFrame = {};
	eastl::array<nvrhi::TimerQueryHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_TimerQueries = {};
	eastl::array<float, Constants::MAX_FRAMES_IN_FLIGHT> m_CpuTimes = {};
};
