#pragma once

#include <PCH.h>

class Renderer;

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

    virtual void CreatePipeline() = 0;
    virtual void ResolutionChanged(uint2 resolution) = 0;
    virtual void Execute(nvrhi::ICommandList* commandList) = 0;
};