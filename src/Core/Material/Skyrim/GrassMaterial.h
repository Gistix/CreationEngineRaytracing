#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"

// Grass uses the standard lighting material data but is routed down the Type::Grass shader path.
struct GrassMaterial : public LightingMaterial
{
	GrassMaterial() = default;

	GrassMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;
};
