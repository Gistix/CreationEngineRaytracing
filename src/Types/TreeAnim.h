#pragma once

#include "PCH.h"

// Per-mesh vanilla TREE_ANIM state: decides when the VertexAnimation pass needs to re-evaluate it. Shared by static, skinned and dynamic meshes.
class TreeAnimState
{
	// Last params sent to the GPU; NaN forces the first evaluation.
	float4 m_LastParams = float4(
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::quiet_NaN());

	// One extra evaluation after the params stop changing so prev positions catch up with live ones.
	bool m_NeedsSettle = false;

public:
	// Fills outParams = (frequency * wind magnitude, amplitude, timer, 0) for VertexAnimationData::Params.
	// Returns true when the GPU must re-evaluate the animation this frame.
	bool Update(RE::BSShaderProperty* shaderProperty, float4& outParams);
};
