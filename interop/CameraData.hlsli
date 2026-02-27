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
    uint2 RenderSize;
    uint2 Pad0;
    float3 PositionPrev;
    uint Pad1;
    
};
VALIDATE_CBUFFER(CameraData, 16);

#endif