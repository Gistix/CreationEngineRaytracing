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
	float3 Vector;
	float Radius;
	float3 Color;
	float InvRadius;
	float3 Direction;
	uint16_t CosOuterAngleHalf;
	uint16_t CosInnerAngleHalf;
	float FadeZone;
	float SizeBias;
	float Fade;
	uint16_t Type;
	uint16_t Flags;
};
VALIDATE_CBUFFER(LightData, 16);

#endif