#ifndef CAMERADATA_HLSL
#define CAMERADATA_HLSL

//#pragma pack_matrix(row_major)

#include "Interop.h"

INTEROP_STRUCT(CameraData, 16)
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;
    float3 Position;
    uint FrameCount;   
};
VALIDATE_CBUFFER(CameraData, 16);

#endif