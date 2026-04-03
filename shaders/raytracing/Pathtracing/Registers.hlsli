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
RWStructuredBuffer<SharcPackedData>         SharcResolvedBuffer         : register(u3);
#else
RWTexture2D<float4>                         Output                      : register(u0);
RWTexture2D<float3>                         DiffuseAlbedo               : register(u1);
RWTexture2D<float3>                         SpecularAlbedo              : register(u2);
RWTexture2D<float4>                         NormalRoughness             : register(u3);
RWTexture2D<float>                          SpecularHitDistance         : register(u4);
#endif

// Stable Planes UAVs (always declared, used when PATH_TRACER_MODE != REFERENCE)
RWTexture2DArray<uint>                      StablePlanesHeaderUAV       : register(u5);
RWStructuredBuffer<StablePlane>             StablePlanesBufferUAV       : register(u6);
RWTexture2D<float4>                         StableRadianceUAV           : register(u7);

#if !(defined(SHARC) && SHARC_UPDATE)
// PT Motion Vectors output (written by BUILD/REFERENCE pass)
RWTexture2D<float4>                         MotionVectors               : register(u8);

// PT Depth output (clip-space depth, written by BUILD/REFERENCE pass)
RWTexture2D<float>                          Depth                       : register(u9);

// ReSTIR GI: Secondary G-Buffer UAVs (written during FILL pass for GI initial samples)
RWTexture2D<float4>                         SecondaryGBufPositionNormal : register(u10);
RWTexture2D<float4>                         SecondaryGBufRadiance       : register(u11);
RWTexture2D<float4>                         SecondaryGBufDiffuseAlbedo  : register(u12);
RWTexture2D<float4>                         SecondaryGBufSpecularRough  : register(u13);

// ReSTIR GI: Packed primary surface data (ping-pong StructuredBuffer)
RWStructuredBuffer<PackedSurfaceData>       SurfaceDataBuffer           : register(u14);
#endif

RaytracingAccelerationStructure             Scene                       : register(t0);
Texture2D<float4>                           SkyHemisphere               : register(t1);
Texture2D<float4>                           WaterFlowMap                : register(t2);
StructuredBuffer<Light>                     Lights                      : register(t3);
StructuredBuffer<Instance>                  Instances                   : register(t4);
StructuredBuffer<Mesh>                      Meshes                      : register(t5);

#if defined(SHARC)
#   if !SHARC_UPDATE
StructuredBuffer<SharcPackedData>           SharcResolvedBuffer         : register(t6);
#   endif

#   if !SHARC_UPDATE
StructuredBuffer<uint64_t>                  SharcHashEntriesBuffer      : register(t7);
#   endif
#endif

StructuredBuffer<Triangle>                  Triangles[]                 : register(t0, space1);
StructuredBuffer<Vertex>                    Vertices[]                  : register(t0, space2);
Texture2D<float4>                           Textures[]                  : register(t0, space3);
RaytracingAccelerationStructure             LightTLAS[]                 : register(t0, space4);
StructuredBuffer<float3>                    PrevPositions[]             : register(t0, space5);

#define HAS_PREV_POSITIONS

SamplerState                                DefaultSampler              : register(s0);

#endif // REGISTERS_HLSLI