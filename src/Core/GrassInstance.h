#pragma once

#include "Core/Instance.h"

#include "Core/Reference/GrassReference.h"

struct GrassInstance : Instance
{
    uint32_t m_InstanceIndex;
    GrassReference::InstanceData m_Data;

    GrassInstance(GrassReference::InstanceData& data, RE::FormID formID, RE::NiAVObject* node, Model* model)
        : Instance(formID, node, model), m_Data(data)
    {

    }

    virtual void UpdateTransform() override;

    virtual float GetAlpha() override { return 1.0f; };
};