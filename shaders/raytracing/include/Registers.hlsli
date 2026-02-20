#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);

RWTexture2D<float4> Output              : register(u0);

RaytracingAccelerationStructure Scene   : register(t0);

SamplerState DefaultSmapler             : register(s0);

StructuredBuffer<Vertex> Vertices[]     : register(t0, space1);
StructuredBuffer<Triangle> Triangles[]  : register(t0, space2);

#endif // REGISTERS_HLSLI