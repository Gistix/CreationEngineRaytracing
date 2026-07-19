#ifndef INSTANCE_HLSL
#define INSTANCE_HLSL

#include "Interop.h"

INTEROP_DATA_STRUCT(Instance, 4)
{
	INTEROP_ROW_MAJOR(float3x4) Transform;
	INTEROP_ROW_MAJOR(float3x4) PrevTransform;
	uint FirstGeometryID;
    uint NumGeometry;
    float Alpha;
};
VALIDATE_ALIGNMENT(InstanceData, 4);

#endif