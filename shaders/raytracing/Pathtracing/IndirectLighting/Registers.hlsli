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
#include "interop/Light.hlsli"

ConstantBuffer<CameraData>                  Camera                      : register(b0);
ConstantBuffer<RaytracingData>              Raytracing                  : register(b1);
ConstantBuffer<FeatureData>                 Features                    : register(b2);

RWTexture2D<half4>                          Output                      : register(u0);

RaytracingAccelerationStructure             Scene                       : register(t0);
Texture2D<float4>                           SkyHemisphere               : register(t1);
Texture2D<float4>                           WaterFlowMap                : register(t2);
StructuredBuffer<Light>                     Lights                      : register(t3);
StructuredBuffer<Instance>                  Instances                   : register(t4);
StructuredBuffer<Mesh>                      Meshes                      : register(t5);
Texture2D<float4>                           SkinDetailNormal            : register(t8);
Texture2D<float4>                           WaterDisplacementMap        : register(t9);
Texture2D<float4>                           ProjNoiseMap                : register(t10);
StructuredBuffer<Transform>                 Transforms                  : register(t11);

Texture2D<float>                            Depth                       : register(t12);
Texture2D<half4>                            Albedo                      : register(t13);
Texture2D<half4>                            EmissiveMetallic            : register(t14);
Texture2D<half4>                            NormalRoughness             : register(t15);
Texture2D<uint>                             Material                    : register(t16);

ByteAddressBuffer                           MeshSlotRemap               : register(t19);
ByteAddressBuffer                           PropertiesBuffer            : register(t20);

ByteAddressBuffer                           Indices[]                   : register(t0, space1);
ByteAddressBuffer                           Vertices[]                  : register(t0, space2);
ByteAddressBuffer                           Materials[]                 : register(t0, space3);

Texture2D<float4>                           Textures[]                  : register(t0, space4);
RaytracingAccelerationStructure             LightTLAS[]                 : register(t0, space5);
StructuredBuffer<float3>                    PrevPositions[]             : register(t0, space6);
TextureCube<float4>                         CubeTextures[]              : register(t0, space7);
StructuredBuffer<float4>                    DynamicPositions[]          : register(t0, space8);

SamplerState                                DefaultSampler              : register(s0);
SamplerState                                ClampSampler                : register(s1);
SamplerState                                PointWrapSampler            : register(s2);

#endif // REGISTERS_HLSLI