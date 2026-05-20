#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "raytracing/include/StablePlanes.hlsli"

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/PackedSurfaceData.hlsli"

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
RWTexture2D<float4>                         Output                      : register(u0);

RWTexture2D<float4>                         NormalRoughness             : register(u1);

RWTexture2D<float4>                         MotionVectors               : register(u2); // PT Motion Vectors output (written by BUILD/REFERENCE pass)
RWTexture2D<float>                          Depth                       : register(u3); // PT Depth output (clip-space depth, written by BUILD/REFERENCE pass)

#   if defined(NRD) | defined(DLSS_RR)
RWTexture2D<float3>                         DiffuseAlbedo               : register(u4);

#       if defined(NRD)
RWTexture2D<float>                          ViewDepth                   : register(u5);
RWTexture2D<float4>                         DiffuseRadiance             : register(u6);
RWTexture2D<float4>                         SpecularRadiance            : register(u7);
RWTexture2D<float3>                         DiffuseFactor               : register(u8);
RWTexture2D<float3>                         SpecularFactor              : register(u9);
#       else
RWTexture2D<float3>                         SpecularAlbedo              : register(u5);
RWTexture2D<float>                          SpecularHitDistance         : register(u6);
#       endif

#   endif

#   if defined(STABLE_PLANES)
// Stable Planes UAVs (always declared, used when PATH_TRACER_MODE != REFERENCE)
RWTexture2DArray<uint>                      StablePlanesHeaderUAV       : register(u10);
RWStructuredBuffer<StablePlane>             StablePlanesBufferUAV       : register(u11);
RWTexture2D<float4>                         StableRadianceUAV           : register(u12);
#   endif

#   if defined(RESTIR_GI)
// ReSTIR GI: Secondary G-Buffer UAVs (written during FILL pass for GI initial samples)
RWTexture2D<float4>                         SecondaryGBufPositionNormal : register(u13);
RWTexture2D<float4>                         SecondaryGBufRadiance       : register(u14);
RWTexture2D<float4>                         SecondaryGBufDiffuseAlbedo  : register(u15);
RWTexture2D<float4>                         SecondaryGBufSpecularRough  : register(u16);

// ReSTIR GI: Packed primary surface data (ping-pong StructuredBuffer)
RWStructuredBuffer<PackedSurfaceData>       SurfaceDataBuffer           : register(u17);
#   endif
#endif

RaytracingAccelerationStructure             Scene                       : register(t0);
Texture2D<float4>                           SkyHemisphere               : register(t1);
Texture2D<float4>                           WaterFlowMap                : register(t2);
StructuredBuffer<Light>                     Lights                      : register(t3);
StructuredBuffer<Instance>                  Instances                   : register(t4);
StructuredBuffer<Mesh>                      Meshes                      : register(t5);

#if defined(SHARC)
StructuredBuffer<SharcPackedData>           SharcResolvedBuffer         : register(t6);

#   if !SHARC_UPDATE
StructuredBuffer<uint64_t>                  SharcHashEntriesBuffer      : register(t7);
#   endif
#endif

StructuredBuffer<Triangle>                  Triangles[]                 : register(t0, space1);
StructuredBuffer<Vertex>                    Vertices[]                  : register(t0, space2);
StructuredBuffer<Material>                  Materials[]                 : register(t0, space3);

Texture2D<float4>                           Textures[]                  : register(t0, space4);
RaytracingAccelerationStructure             LightTLAS[]                 : register(t0, space5);
StructuredBuffer<float3>                    PrevPositions[]             : register(t0, space6);
TextureCube<float4>                         CubeTextures[]              : register(t0, space7);

#define HAS_PREV_POSITIONS

SamplerState                                DefaultSampler              : register(s0);
SamplerState                                ClampSampler                : register(s1);
SamplerState                                PointWrapSampler            : register(s2);

#endif // REGISTERS_HLSLI