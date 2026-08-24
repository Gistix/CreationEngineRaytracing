#pragma once

#include "Renderer/RenderNode.h"

class RenderGraph
{
    Renderer* m_Renderer;

    eastl::unordered_map<eastl::string, nvrhi::ResourceHandle> m_Resources;
    eastl::vector<RenderNode> m_Nodes;

public:
    RenderGraph(Renderer* renderer);

    void AddNode(RenderNode node)
    {
        m_Nodes.push_back(eastl::move(node));
    }

    void ClearNodes()
    {
        m_Nodes.clear();
    }

    const eastl::vector<RenderNode>& GetNodes() const { return m_Nodes; }
    eastl::vector<RenderNode>& GetNodes() { return m_Nodes; }

    template<typename T>
    T* GetPass()
    {
        for (auto& node : m_Nodes)
        {
            if (auto* pass = node.GetPass<T>())
                return pass;
        }
        return nullptr;
    }

    template<typename T>
    RenderNode* GetNode()
    {
        for (auto& node : m_Nodes)
        {
            if (node.GetPass<T>())
                return &node;
        }
        return nullptr;
    }

    template<typename T>
    bool SetEnabled(bool enabled)
    {
        for (auto& node : m_Nodes)
        {
            if (node.GetPass<T>())
            {
                node.SetEnabled(enabled);
                return true;
            }
        }
        return false;
    }

    nvrhi::IResource* GetResource(eastl::string name)
    {
        auto it = m_Resources.find(name);
        if (it == m_Resources.end())
            return nullptr;
        return it->second.Get();
    }

    nvrhi::ITexture* GetTexture(eastl::string name)
    {
        auto it = m_Resources.find(name);
        if (it == m_Resources.end())
            return nullptr;
        return reinterpret_cast<nvrhi::ITexture*>(it->second.Get());
    }

    void ResolutionChanged(uint2 resolution);

    void SettingsChanged(const Settings& settings);

    void Execute(nvrhi::ICommandList* commandList);

    template <typename Func>
    void ForEach(Func&& func)
    {
        for (auto& node : m_Nodes)
        {
            std::invoke(func, &node);
        }
    }
};
