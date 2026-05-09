#pragma once

#include "core/Instance.h"

struct GrassInstance : Instance
{
    uint32_t m_InstanceIndex;
    RE::BGSGrassManager::InstanceData m_Data;

    GrassInstance(RE::BGSGrassManager::InstanceData& data, RE::NiAVObject* node, Model* model)
        : Instance(0, node, model), m_Data(data)
    {

    }

	virtual void UpdateTransform() override;

    virtual float GetAlpha() override { return 1.0f; };
};