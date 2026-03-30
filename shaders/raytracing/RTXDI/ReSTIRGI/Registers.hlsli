#ifndef RESTIRGI_REGISTERS_HLSLI
#define RESTIRGI_REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/ReSTIRGIData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/PackedSurfaceData.hlsli"

// Constant buffers
ConstantBuffer<CameraData>    Camera      : register(b0);
ConstantBuffer<ReSTIRGIData>  g_ReSTIRGI  : register(b1);
ConstantBuffer<FeatureData>   Features    : register(b2);

// Scene acceleration structure (for visibility rays in bias correction)
RaytracingAccelerationStructure SceneBVH  : register(t0);

// Current frame G-buffer
Texture2D<float>  CurrentDepth            : register(t1);
Texture2D<float4> CurrentNormals          : register(t2);  // xyz=normal, w=roughness

// Previous frame G-buffer
Texture2D<float>  PreviousDepth           : register(t3);
Texture2D<float4> PreviousNormals         : register(t4);

// Secondary surface data (written by path tracer FILL pass)
Texture2D<float4> SecondaryPositionNormal : register(t5);  // xyz=worldPos, w=packedNormal
Texture2D<float4> SecondaryRadiance       : register(t6);  // xyz=radiance, w=samplePdf
Texture2D<float4> SecondaryDiffuseAlbedo  : register(t7);  // xyz=diffuseAlbedo
Texture2D<float4> SecondarySpecularRough  : register(t8);  // xyz=specularF0, w=roughness

// Neighbor offset buffer for spatial resampling
Buffer<float2>    NeighborOffsets         : register(t9);

// Motion vectors for temporal reprojection
Texture2D<float4> MotionVectors           : register(t10);

// Primary surface material data (written by path tracer)
Texture2D<float3> PrimaryDiffuseAlbedo    : register(t11);
Texture2D<float3> PrimarySpecularAlbedo   : register(t12);

// Packed primary surface data (ping-pong StructuredBuffer from path tracer)
StructuredBuffer<PackedSurfaceData> SurfaceDataBuffer : register(t13);

// GI reservoir buffer (read/write)
RWStructuredBuffer<RTXDI_PackedGIReservoir> GIReservoirs : register(u0);

// Output: MainTexture (final shading reads, adds GI, writes back)
RWTexture2D<float4> OutputRadiance        : register(u1);

SamplerState DefaultSampler               : register(s0);

// Define macros required by RTXDI before including its headers
#define RTXDI_GI_RESERVOIR_BUFFER GIReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER NeighborOffsets
#define RTXDI_ENABLE_BOILING_FILTER 1
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8
#define RTXDI_ENABLE_STORE_RESERVOIR 1

#endif // RESTIRGI_REGISTERS_HLSLI
