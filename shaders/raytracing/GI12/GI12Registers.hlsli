/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#ifndef GI12_REGISTERS_HLSLI
#define GI12_REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/GI12Data.hlsli"
#include "interop/Light.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Transform.hlsli"

// Constant Buffers
ConstantBuffer<CameraData>                     Camera                 : register(b0);
ConstantBuffer<RaytracingData>                 Raytracing             : register(b1);
ConstantBuffer<GI12Constants>                  GI12CB                 : register(b2);
ConstantBuffer<ScreenProbesConstants>          ScreenProbesCB         : register(b3);
ConstantBuffer<HashGridCacheConstants>         HashGridCacheCB        : register(b4);
ConstantBuffer<WorldSpaceReSTIRConstants>      WorldSpaceReSTIRCB     : register(b5);
ConstantBuffer<GlossyReflectionsConstants>     GlossyReflectionsCB    : register(b6);
ConstantBuffer<GlossyReflectionsAtrousConstants> GlossyAtrousCB       : register(b7);
ConstantBuffer<GIDenoiserConstants>            GIDenoiserCB           : register(b8);

// Samplers
SamplerState LinearWrapSampler   : register(s0);
SamplerState LinearClampSampler  : register(s1);
SamplerState PointWrapSampler    : register(s2);
SamplerState PointClampSampler   : register(s3);

// Scene SRVs
RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float>                Depth            : register(t1);
Texture2D<float4>               Albedo           : register(t2);
Texture2D<snorm float4>         NormalRoughness  : register(t3);
Texture2D<float3>               VAOMAO           : register(t4);
Texture2D<float2>               MotionVectors    : register(t5);
Texture2D<float3>               FaceNormals      : register(t6);
Texture2D<float4>               SkyHemisphere    : register(t7);
StructuredBuffer<Light>         Lights           : register(t8);
StructuredBuffer<Instance>      Instances        : register(t9);
StructuredBuffer<Mesh>          Meshes           : register(t10);

#endif // GI12_REGISTERS_HLSLI
