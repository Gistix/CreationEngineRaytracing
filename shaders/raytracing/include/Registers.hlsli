#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Light.hlsli"

ConstantBuffer<CameraData> Camera           : register(b0);
ConstantBuffer<RaytracingData> Raytracing   : register(b1);

RWTexture2D<float4> Output                  : register(u0);

RaytracingAccelerationStructure Scene       : register(t0);
StructuredBuffer<Light> Lighjts             : register(t1);
StructuredBuffer<Instance> Instances        : register(t2);
StructuredBuffer<Mesh> Meshes               : register(t3);

StructuredBuffer<Triangle> Triangles[]      : register(t0, space1);

StructuredBuffer<Vertex> Vertices[]         : register(t0, space2);

Texture2D<float4> Textures[]                : register(t0, space3);
TextureCube<float4> CubeTextures[]          : register(t0, space6);

SamplerState DefaultSmapler                 : register(s0);

#endif // REGISTERS_HLSLI