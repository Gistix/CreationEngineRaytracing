#ifndef VERTEX_HLSL
#define VERTEX_HLSL

#include "Interop.h"
#include "ubyte4.hlsli"

struct Vertex
{
	float3 Position;
	half2 Texcoord0;
	half3 Normal;
	half3 Bitangent;
	float Handedness;
#if defined(BAKED_TEXTURES)	// Probably misaligned, but no being used atm
	ubyte4f Albedo;
    byte16f Roughness;
    byte16f Metallic;
#else
	ubyte4f Color;
	ubyte4f LandBlend0;
	ubyte4f LandBlend1;
#endif
};
VALIDATE_ALIGNMENT(Vertex, 4);

#endif