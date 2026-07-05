#ifndef RESTIRPT_REGISTERS_HLSLI
#define RESTIRPT_REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/ReSTIRPTData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/PackedSurfaceData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Light.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// Constant buffers
ConstantBuffer<CameraData>     Camera      : register(b0);
ConstantBuffer<ReSTIRPTData>   g_ReSTIRPT  : register(b1);
ConstantBuffer<FeatureData>    Features    : register(b2);
ConstantBuffer<RaytracingData> Raytracing  : register(b3);
#define CERT_HAS_FEATURES

// Scene acceleration structure (for visibility rays)
RaytracingAccelerationStructure SceneBVH   : register(t0);
#define Scene SceneBVH

Texture2D<float4> SkyHemisphere            : register(t1);
Texture2D<float4> WaterFlowMap             : register(t2);
StructuredBuffer<Light> Lights             : register(t3);
StructuredBuffer<Instance> Instances       : register(t4);
StructuredBuffer<Mesh> Meshes              : register(t5);
Texture2D<float4> PhysicalSkyTrLUT          : register(t8);
Texture2D<float4> SkinDetailNormal         : register(t9);

// Current frame G-buffer
Texture2D<float>  CurrentDepth             : register(t10);
Texture2D<float4> CurrentNormals           : register(t11);  // xyz=normal, w=roughness

// Previous frame G-buffer
Texture2D<float>  PreviousDepth            : register(t12);
Texture2D<float4> PreviousNormals          : register(t13);

// Neighbor offset buffer for spatial resampling
Buffer<float2>    NeighborOffsets           : register(t14);

// Motion vectors for temporal reprojection
Texture2D<float4> MotionVectors            : register(t15);

// Packed primary surface data (ping-pong StructuredBuffer from path tracer)
StructuredBuffer<PackedSurfaceData> SurfaceDataBuffer : register(t16);

// Primary surface material data (for denoiser guide buffers)
Texture2D<float3> PrimaryDiffuseAlbedo     : register(t17);
Texture2D<float3> PrimarySpecularAlbedo    : register(t18);

StructuredBuffer<Triangle> Triangles[]     : register(t0, space1);
ByteAddressBuffer Vertices[]              : register(t0, space2);
ByteAddressBuffer Materials[]              : register(t0, space3);
Texture2D<float4> Textures[]               : register(t0, space4);
StructuredBuffer<float3> PrevPositions[]   : register(t0, space6);
TextureCube<float4> CubeTextures[]         : register(t0, space7);
StructuredBuffer<float4> DynamicPositions[] : register(t0, space8);

// PT reservoir buffer (read/write)
RWStructuredBuffer<RTXDI_PackedPTReservoir> PTReservoirs : register(u0);

// Output: MainTexture (final shading reads, adds indirect, writes back)
RWTexture2D<float4> OutputRadiance         : register(u1);

SamplerState DefaultSampler                : register(s0);
SamplerState ClampSampler                  : register(s1);
SamplerState PointWrapSampler              : register(s2);

// Define macros required by RTXDI PT before including its headers
#define RTXDI_PT_RESERVOIR_BUFFER PTReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER NeighborOffsets
#define RTXDI_ENABLE_BOILING_FILTER 1
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8

#endif // RESTIRPT_REGISTERS_HLSLI
