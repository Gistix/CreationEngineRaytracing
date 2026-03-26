#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "raytracing/include/StablePlanes.hlsli"

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

// PT Motion Vectors output (written by BUILD/REFERENCE pass)
RWTexture2D<float4>                         MotionVectors               : register(u8);

// PT Depth output (clip-space depth, written by BUILD/REFERENCE pass)
RWTexture2D<float>                          Depth                       : register(u9);

RaytracingAccelerationStructure             Scene                       : register(t0);
Texture2D<float4>                           SkyHemisphere               : register(t1);
StructuredBuffer<Light>                     Lights                      : register(t2);
StructuredBuffer<Instance>                  Instances                   : register(t3);
StructuredBuffer<Mesh>                      Meshes                      : register(t4);

#if defined(SHARC)
StructuredBuffer<SharcPackedData>           SharcResolvedBuffer         : register(t5);

#   if !SHARC_UPDATE
StructuredBuffer<uint64_t>                  SharcHashEntriesBuffer      : register(t6);
#   endif
#endif

StructuredBuffer<Triangle>                  Triangles[]                 : register(t0, space1);
StructuredBuffer<Vertex>                    Vertices[]                  : register(t0, space2);
Texture2D<float4>                           Textures[]                  : register(t0, space3);
RaytracingAccelerationStructure             LightTLAS[]                 : register(t0, space4);

SamplerState                                DefaultSampler              : register(s0);

#endif // REGISTERS_HLSLI