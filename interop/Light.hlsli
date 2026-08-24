#ifndef LIGHT_HLSL
#define LIGHT_HLSL

#include "Interop.h"

namespace LightType
{
	static const uint16_t Directional = 0;
	static const uint16_t Point = 1;
	static const uint16_t Spot = 2;
}

namespace LightFlags
{
	static const uint16_t ISL = (1 << 0);
	static const uint16_t LinearLight = (1 << 1);
}

INTEROP_DATA_STRUCT(Light, 16)
{
	float3 Position;
    float InvRadius;
    half3 Color;
	half Radius;
    float CosOuterAngle;
    float CosInnerAngle;
	float3 Direction;
	float FadeZone;
	float SizeBias;
	float Fade;
	uint16_t Type;
	uint16_t Flags;
    float Pad;
};
VALIDATE_CBUFFER(LightData, 16);

#endif