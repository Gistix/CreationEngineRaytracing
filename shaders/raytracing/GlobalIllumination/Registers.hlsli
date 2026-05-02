#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Light.hlsli"
#include "interop/SHaRCData.hlsli"

#include "interop/SharcTypes.h"

ConstantBuffer<CameraData>                  Camera                      : register(b0);
ConstantBuffer<RaytracingData>              Raytracing                  : register(b1);
ConstantBuffer<FeatureData>                 Features                    : register(b2);
ConstantBuffer<SHaRCData>                   SHaRC                       : register(b3);

#if defined(SHARC) && SHARC_UPDATE
RWStructuredBuffer<uint64_t>                SharcHashEntriesBuffer      : register(u0);
RWStructuredBuffer<uint>                    SharcLockBuffer             : register(u1);
RWStructuredBuffer<SharcAccumulationData>   SharcAccumulationBuffer     : register(u2);
#else

#   if defined(RAW_RADIANCE)
RWTexture2D<float4>                         DiffuseOutput               : register(u0);
RWTexture2D<float4>                         SpecularOutput              : register(u1);
#       if defined(NRD)
RWTexture2D<float>                          ViewDepth                   : register(u2);
RWTexture2D<float3>                         DiffuseFactor               : register(u3);
RWTexture2D<float3>                         SpecularFactor              : register(u4);
#       endif
#   else
RWTexture2D<float4>                         Output                      : register(u0);

#       if defined(DLSS_RR)
RWTexture2D<float3>                         SpecularAlbedo              : register(u1);
RWTexture2D<float>                          SpecularHitDistance         : register(u2);
#       endif

#   endif

#endif

RaytracingAccelerationStructure             Scene                       : register(t0);
Texture2D<float4>                           SkyHemisphere               : register(t1);
Texture2D<float4>                           WaterFlowMap                : register(t2);
StructuredBuffer<Light>                     Lights                      : register(t3);
StructuredBuffer<Instance>                  Instances                   : register(t4);
StructuredBuffer<Mesh>                      Meshes                      : register(t5);

Texture2D<float>                            Depth                       : register(t6); // RENDER_TARGETS_DEPTHSTENCIL::kMAIN - R32
Texture2D<float4>                           Albedo                      : register(t7); // ALBEDO - True albedo (not modulated by metalness)
Texture2D<snorm float4>                     NormalRoughness             : register(t8); // "NORMALROUGHNESS" - World normals and roughness - Processed from GBuffer encoded view normals and smoothness
Texture2D<unorm float4>                     MAO                         : register(t9); // MASKS2 - Metalness and AO
Texture2D<float3>                           FaceNormals                 : register(t10);

#if defined(SHARC)
StructuredBuffer<SharcPackedData>           SharcResolvedBuffer         : register(t11);

#   if !SHARC_UPDATE
StructuredBuffer<uint64_t>                  SharcHashEntriesBuffer      : register(t12);
#   endif
#endif

StructuredBuffer<Triangle>                  Triangles[]                 : register(t0, space1);
StructuredBuffer<Vertex>                    Vertices[]                  : register(t0, space2);
Texture2D<float4>                           Textures[]                  : register(t0, space3);
//RaytracingAccelerationStructure             LightTLAS[]                 : register(t0, space4);
TextureCube<float4>                         CubeTextures[]              : register(t0, space6);

SamplerState                                DefaultSampler              : register(s0);
SamplerState                                ClampSampler                : register(s1);

#endif // REGISTERS_HLSLI