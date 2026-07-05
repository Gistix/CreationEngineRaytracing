#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

#include "interop/Light.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Triangle.hlsli"

SamplerState DefaultSampler : register(s0);
SamplerState ClampSampler : register(s1);
SamplerState PointWrapSampler : register(s2);

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<RaytracingData> Raytracing : register(b1);
ConstantBuffer<FeatureData> Features : register(b2);

RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float4> SkyHemisphere : register(t1);
StructuredBuffer<Light> Lights : register(t2);
StructuredBuffer<Instance> Instances : register(t4);
StructuredBuffer<Mesh> Meshes : register(t5);

StructuredBuffer<Triangle> Triangles[] : register(t0, space1);
ByteAddressBuffer Vertices[] : register(t0, space2);
ByteAddressBuffer Materials[] : register(t0, space3);

Texture2D<float4> Textures[] : register(t0, space4);

StructuredBuffer<float4> DynamicPositions[] : register(t0, space8);

RWTexture2D<float4> Output : register(u0);

#endif // REGISTERS_HLSLI