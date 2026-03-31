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

	// Returns the first pass of type T found in this node or any child nodes.
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

	// Returns the first node with a pass of type T.
	template<typename T>
	RenderNode* GetNode()
	{
		if (auto* pass = GetImmediatePass<T>())
			return this;

		for (auto& child : m_Children)
		{
			if (auto* node = child.GetNode<T>())
				return node;
		}

		return nullptr;
	}

	// Sets the first found node with a pass of type T to enabled/disabled.
	template<typename T>
	bool SetEnabled(bool enabled)
	{
		if (GetImmediatePass<T>())
		{
			SetEnabled(enabled); // non-template version
			return true;
		}

		for (auto& child : m_Children)
		{
			if (child.SetEnabled<T>(enabled))
				return true;
		}

		return false;
	}

	// Sets this node to enabled/disabled.
	void SetEnabled(bool enabled) { 
		logger::info("RenderNode::SetEnabled - Setting {} enabled to {}", m_Name, enabled);
		m_Enabled = enabled; 
	}

	void AddNode(RenderNode renderNode);

	void ResolutionChanged(uint2 resolution) override;

	void SettingsChanged(const Settings& settings) override;

	void Execute(nvrhi::ICommandList* commandList) override;
};
