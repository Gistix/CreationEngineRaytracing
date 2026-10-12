#ifndef DDGI_REGISTERS_HLSLI
#define DDGI_REGISTERS_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/DDGIData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Transform.hlsli"
#include "interop/Light.hlsli"

#include "include/Vulkan.hlsli"

ConstantBuffer<CameraData>                  Camera                      : register(b0);
ConstantBuffer<DDGIData>                    DDGIParams                  : register(b1);
ConstantBuffer<FeatureData>                 Features                    : register(b2);
ConstantBuffer<RaytracingData>              Raytracing                  : register(b3);

RWTexture2DArray<float4>                    RayData                     : register(u0);

RaytracingAccelerationStructure             Scene                       : register(t0);
Texture2D<float4>                           SkyHemisphere               : register(t1);
StructuredBuffer<Light>                     Lights                      : register(t2);
StructuredBuffer<Instance>                  Instances                   : register(t3);
StructuredBuffer<Mesh>                      Meshes                      : register(t4);
StructuredBuffer<Transform>                 Transforms                  : register(t5);
StructuredBuffer<uint>                      InstanceLightList           : register(t6);

Texture2DArray<float4>                      ProbeData                   : register(t7);
Texture2DArray<float4>                      ProbeIrradiance             : register(t8);
Texture2DArray<float4>                      ProbeDistance               : register(t9);

ByteAddressBuffer                           MeshSlotRemap               : register(t10);
ByteAddressBuffer                           PropertiesBuffer            : register(t11);
Texture2D<float4>                           WaterDisplacementMap        : register(t12);
Texture2D<float4>                           SkinDetailNormal            : register(t13);
Texture2D<float4>                           ProjNoiseMap                : register(t14);
Texture2D<float4>                           WaterFlowMap                : register(t15);

ByteAddressBuffer                           Indices[]                   : register(t0, space1);
ByteAddressBuffer                           Vertices[]                  : register(t0, space2);
ByteAddressBuffer                           Materials[]                 : register(t0, space3);
Texture2D<float4>                           Textures[]                  : register(t0, space4);

VK_BINDING(5, 0) TextureCube<float4>        CubeTextures[]              : register(t0, space7);
VK_BINDING(6, 0) StructuredBuffer<float4>   DynamicPositions[]          : register(t0, space8);

SamplerState                                DefaultSampler              : register(s0);
SamplerState                                ClampSampler                : register(s1);
SamplerState                                PointWrapSampler            : register(s2);
SamplerState                                BilinearClampSampler        : register(s3);

#endif // DDGI_REGISTERS_HLSLI
