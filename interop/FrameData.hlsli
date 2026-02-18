#ifndef FRAMEDATA_HLSL
#define FRAMEDATA_HLSL

//#pragma pack_matrix(row_major)

#include "Interop.h"

INTEROP_STRUCT(FrameData, 16)
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;
    float3 Position;
    uint FrameCount;   
};
VALIDATE_CBUFFER(FrameData, 16);

#endif