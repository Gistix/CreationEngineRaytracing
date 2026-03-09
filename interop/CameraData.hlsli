#ifndef CAMERADATA_HLSL
#define CAMERADATA_HLSL

#include "Interop.h"

INTEROP_STRUCT(CameraData, 16)
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;
    float3 Position;
    uint FrameIndex;
    uint2 ScreenSize;
    uint2 RenderSize;
    float3 PositionPrev;
    uint Pad1;
    float4x4 ViewProj;
    float4x4 PrevViewProj;
    float4x4 PrevViewInverse;
};
VALIDATE_CBUFFER(CameraData, 16);

#endif