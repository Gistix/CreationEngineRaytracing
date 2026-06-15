#ifndef RESTIRPT_REGISTERS_HLSLI
#define RESTIRPT_REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/ReSTIRPTData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/PackedSurfaceData.hlsli"

// Constant buffers
ConstantBuffer<CameraData>     Camera      : register(b0);
ConstantBuffer<ReSTIRPTData>   g_ReSTIRPT  : register(b1);
ConstantBuffer<FeatureData>    Features    : register(b2);
#define CERT_HAS_FEATURES

// Scene acceleration structure (for visibility rays)
RaytracingAccelerationStructure SceneBVH   : register(t0);

// Current frame G-buffer
Texture2D<float>  CurrentDepth             : register(t1);
Texture2D<float4> CurrentNormals           : register(t2);  // xyz=normal, w=roughness

// Previous frame G-buffer
Texture2D<float>  PreviousDepth            : register(t3);
Texture2D<float4> PreviousNormals          : register(t4);

// Neighbor offset buffer for spatial resampling
Buffer<float2>    NeighborOffsets           : register(t5);

// Motion vectors for temporal reprojection
Texture2D<float4> MotionVectors            : register(t6);

// Packed primary surface data (ping-pong StructuredBuffer from path tracer)
StructuredBuffer<PackedSurfaceData> SurfaceDataBuffer : register(t7);

// Primary surface material data (for denoiser guide buffers)
Texture2D<float3> PrimaryDiffuseAlbedo     : register(t8);
Texture2D<float3> PrimarySpecularAlbedo    : register(t9);

// PT reservoir buffer (read/write)
RWStructuredBuffer<RTXDI_PackedPTReservoir> PTReservoirs : register(u0);

// Output: MainTexture (final shading reads, adds indirect, writes back)
RWTexture2D<float4> OutputRadiance         : register(u1);

SamplerState DefaultSampler                : register(s0);

// Define macros required by RTXDI PT before including its headers
#define RTXDI_PT_RESERVOIR_BUFFER PTReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER NeighborOffsets
#define RTXDI_ENABLE_BOILING_FILTER 1
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8

#endif // RESTIRPT_REGISTERS_HLSLI
