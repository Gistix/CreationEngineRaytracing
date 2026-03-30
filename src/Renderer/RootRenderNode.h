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

	void AttachRenderNode(RenderNode* renderNode);

	void DetachRenderNode(RenderNode* renderNode);

	void ResolutionChanged(uint2 resolution) override;

	void SettingsChanged(const Settings& settings) override;

	void Execute(nvrhi::ICommandList* commandList) override;
};
