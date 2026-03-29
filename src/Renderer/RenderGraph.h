#pragma once

#include "Renderer/RootRenderNode.h"
#include "Renderer/RenderNode.h"
#include "Renderer/FrameGraphBuilder.h"

class RenderGraph
{
    Renderer* m_Renderer;

    eastl::unordered_map<eastl::string, nvrhi::ResourceHandle> m_Resources;
    eastl::unique_ptr<RootRenderNode> m_RootNode = nullptr;

public:
    RenderGraph(Renderer* m_Renderer);

    RootRenderNode* GetRootNode() const { return m_RootNode.get(); }

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

    void Setup(const Settings& settings);

    void Execute(nvrhi::ICommandList* commandList);
};
