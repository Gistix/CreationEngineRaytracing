#pragma once

#include <PCH.h>

#include "Types/Settings.h"

class Renderer;
class FrameGraphBuilder;
class PassResources;
class FrameGraph;

class RenderPass
{
private:
    Renderer* m_Renderer = nullptr;

protected:
    Renderer* GetRenderer() { return m_Renderer; }

public:
    RenderPass(Renderer* renderer) : m_Renderer(renderer) {}
    RenderPass() = delete;

    virtual ~RenderPass() = default;

    virtual void Setup([[maybe_unused]] FrameGraphBuilder& builder, [[maybe_unused]] const Settings& settings) {};
    virtual void CreatePipeline() {};
    virtual void SettingsChanged([[maybe_unused]] const Settings& settings) {};
    virtual void ResolutionChanged([[maybe_unused]] uint2 resolution) {};
    virtual void Execute(nvrhi::ICommandList* commandList) = 0;
};
