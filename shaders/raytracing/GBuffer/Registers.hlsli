#ifndef REGISTERS_HLSLI
#define REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Transform.hlsli"

ConstantBuffer<CameraData>                  Camera                      : register(b0);
ConstantBuffer<RaytracingData>              Raytracing                  : register(b1);
ConstantBuffer<FeatureData>                 Features                    : register(b2);


RaytracingAccelerationStructure             Scene                       : register(t0);
StructuredBuffer<Instance>                  Instances                   : register(t1);
StructuredBuffer<Mesh>                      Meshes                      : register(t2);
StructuredBuffer<Transform>             Transforms                  : register(t3);

StructuredBuffer<Triangle>                  Triangles[]                 : register(t0, space1);
ByteAddressBuffer                           Vertices[]                  : register(t0, space2);
ByteAddressBuffer                           Materials[]                 : register(t0, space3);

Texture2D<float4>                           Textures[]                  : register(t0, space4);
TextureCube<float4>                         CubeTextures[]              : register(t0, space7);
StructuredBuffer<float4>                    DynamicPositions[]          : register(t0, space8);

RWTexture2D<float>                          Depth                       : register(u0);
RWTexture2D<float3>                         MotionVectors               : register(u1);
RWTexture2D<float4>                         Albedo                      : register(u2);
RWTexture2D<float4>                         NormalRoughness             : register(u3);
RWTexture2D<float4>                         EmissiveMetallic            : register(u4);

SamplerState                                DefaultSampler              : register(s0);
SamplerState                                ClampSampler                : register(s1);
SamplerState                                PointWrapSampler            : register(s2);

#endif // REGISTERS_HLSLI