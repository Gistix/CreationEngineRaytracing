#ifndef LANDLOD_UPDATE_HLSL
#define LANDLOD_UPDATE_HLSL

#include "Interop.h"

INTEROP_STRUCT(LandLODUpdate, 4)
{
	uint Index;
    uint VertexCount;
    INTEROP_ROW_MAJOR(float3x4) MeshTransform;
    INTEROP_ROW_MAJOR(float3x4) InstanceTransform;
};
VALIDATE_ALIGNMENT(LandLODUpdate, 4);

#endif