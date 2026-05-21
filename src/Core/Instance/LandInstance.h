#pragma once

#include "core/Instance.h"

struct LandInstance : Instance
{
    LandInstance(RE::FormID formID, RE::NiAVObject* node, Model* model)
        : Instance(formID, node, model)
    {

    }

	virtual bool IsHidden() const override;
};