#pragma once

#include "Pass/RenderPass.h"
#include "Renderer/IRenderNode.h"
#include "RenderNode.h"

class RootRenderNode : public IRenderNode
{
	bool m_Enabled = true;
	eastl::vector<RenderNode*> m_Children;

public:

	bool HasRenderNode() const { return !m_Children.empty(); }

	template<typename T>
	T* GetPass()
	{
		for (auto& child : m_Children)
		{
			if (auto* childPass = child->GetPass<T>())
				return childPass;
		}

		return nullptr;
	}

	template<typename T>
	RenderNode* GetNode()
	{
		for (auto& child : m_Children)
		{
			if (auto* node = child.GetNode<T>())
				return node;
		}

		return nullptr;
	}

	template<typename T>
	bool SetEnabled(bool enabled)
	{
		for (auto& child : m_Children)
		{
			if (child->SetEnabled<T>(enabled))
				return true;
		}

		return false;
	}

	void AttachRenderNode(RenderNode* renderNode);

	void DetachRenderNode(RenderNode* renderNode);

	void ResolutionChanged(uint2 resolution) override;

	void SettingsChanged(const Settings& settings) override;

	void Execute(nvrhi::ICommandList* commandList) override;

	template <typename Func>
	void ForEach(Func&& func)
	{
		if (!m_Enabled)
			return;

		for (auto* child : m_Children)
		{
			child->ForEach(func);
		}
	}
};
