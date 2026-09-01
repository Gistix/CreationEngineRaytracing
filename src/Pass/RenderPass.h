#pragma once

#include <PCH.h>

#include "Types/Settings.h"

#include "Constants.h"

class Renderer;
class PassResources;
class FrameGraph;

class RenderPass
{
private:
    Renderer* m_Renderer = nullptr;

protected:
    Renderer* GetRenderer() const { return m_Renderer; }
    bool m_Enabled = true;
    bool m_Initialized = false;

public:
    RenderPass(Renderer* renderer) : m_Renderer(renderer) {}
    RenderPass() = delete;

    virtual ~RenderPass() = default;

    virtual bool IsEnabled() const { return m_Enabled; }
    virtual void SetEnabled(bool enabled) { m_Enabled = enabled; }

    virtual void Initialize() { CreatePipeline(); }
    virtual void CreatePipeline() {};

    void EnsureInitialized()
    {
        if (!m_Initialized && IsEnabled()) {
            Initialize();
            m_Initialized = true;
        }
    }

    virtual void SettingsChanged([[maybe_unused]] const Settings& settings) {};
    virtual void ResolutionChanged([[maybe_unused]] uint2 resolution) {};
    virtual void Execute(nvrhi::ICommandList* commandList) = 0;
};
