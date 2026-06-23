#ifndef VERTEX_HLSL
#define VERTEX_HLSL

#include "Interop.h"
#include "ubyte4.hlsli"

struct Vertex
{
	float3 Position;
	half2 Texcoord0;
	half3 Normal;
	half3 Tangent;
	float Handedness;
	ubyte4f Color;
	ubyte4f LandBlend0;
	ubyte4f LandBlend1;
};
VALIDATE_ALIGNMENT(Vertex, 4);

#endif