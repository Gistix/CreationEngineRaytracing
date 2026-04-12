#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"

ConstantBuffer<CameraData>                  Camera                      : register(b0);
ConstantBuffer<RaytracingData>              Raytracing                  : register(b1);
ConstantBuffer<FeatureData>                 Features                    : register(b2);


RaytracingAccelerationStructure             Scene                       : register(t0);
StructuredBuffer<Instance>                  Instances                   : register(t1);
StructuredBuffer<Mesh>                      Meshes                      : register(t2);

StructuredBuffer<Triangle>                  Triangles[]                 : register(t0, space1);
StructuredBuffer<Vertex>                    Vertices[]                  : register(t0, space2);
Texture2D<float4>                           Textures[]                  : register(t0, space3);
TextureCube<float4>                         CubeTextures[]              : register(t0, space6);

RWTexture2D<float>                          Depth                       : register(u0);
RWTexture2D<float3>                         MotionVectors               : register(u1);
RWTexture2D<float4>                         Albedo                      : register(u2);
RWTexture2D<float4>                         NormalRoughness             : register(u3);
RWTexture2D<float4>                         EmissiveMetallic            : register(u4);

SamplerState                                DefaultSampler              : register(s0);


#endif // REGISTERS_HLSLI