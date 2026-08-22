#pragma once

#include "PCH.h"

struct CameraRuntimeData
{
	float4x4 viewMat{};
	float4x4 projMat{};
	float4x4 viewProjMatrixUnjittered{};
	float4x4 previousViewProjMatrixUnjittered{};
	float3 posAdjust{};
	float3 previousPosAdjust{};
};
