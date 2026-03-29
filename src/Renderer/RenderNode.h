#pragma once

#include "Pass/RenderPass.h"
#include "Renderer/IRenderNode.h"

class RenderNode : public IRenderNode
{
	bool m_Enabled = true;
	eastl::string m_Name;
	eastl::unique_ptr<RenderPass> m_RenderPass;
	eastl::vector<RenderNode> m_Children;

public:
	RenderNode(bool enabled, const char* name) :
		m_Enabled(enabled), m_Name(name) {
	}

	RenderNode(bool enabled, const char* name, RenderPass* renderPass) :
		m_Enabled(enabled), m_Name(name), m_RenderPass(renderPass) {
	}

	RenderNode(bool enabled, const char* name, eastl::unique_ptr<RenderPass> renderPass) :
		m_Enabled(enabled), m_Name(name), m_RenderPass(eastl::move(renderPass)) {
	}

	RenderNode(bool enabled, const char* name, RenderPass* renderPass, eastl::vector<RenderNode>& children) :
		m_Enabled(enabled), m_Name(name), m_RenderPass(renderPass), m_Children(eastl::move(children)) {
	}

	RenderNode(bool enabled, const char* name, eastl::unique_ptr<RenderPass> renderPass, eastl::vector<RenderNode>& children) :
		m_Enabled(enabled), m_Name(name), m_RenderPass(eastl::move(renderPass)), m_Children(eastl::move(children)) {
	}

	template<typename T>
	T* GetImmediatePass()
	{
		static_assert(eastl::is_base_of_v<RenderPass, T>,
			"T must derive from RenderPass");

		if (!m_RenderPass)
			return nullptr;

		return dynamic_cast<T*>(m_RenderPass.get());
	}

	template<typename T>
	T* GetPass()
	{
		if (auto* pass = GetImmediatePass<T>())
			return pass;

		for (auto& child : m_Children)
		{
			if (auto* childPass = child.GetPass<T>())
				return childPass;
		}

		return nullptr;
	}

	void AddNode(RenderNode renderNode);

	void Setup(class FrameGraphBuilder& builder, const Settings& settings);

	void ResolutionChanged(uint2 resolution) override;

	void SettingsChanged(const Settings& settings) override;

	void Execute(nvrhi::ICommandList* commandList) override;
};
