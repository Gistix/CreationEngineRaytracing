#pragma once

#include "core/Instance.h"

struct TreeLODInstance : Instance
{
    RE::BGSDistantTreeBlock::InstanceData* m_Data;

    TreeLODInstance(RE::BGSDistantTreeBlock::InstanceData* data, RE::NiAVObject* node, Model* model)
        : Instance(0, node, model), m_Data(data)
    {

    }

	virtual void UpdateTransform() override;

    virtual float GetAlpha() override { return m_Data->alpha; };
};