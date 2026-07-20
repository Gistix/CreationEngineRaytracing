#ifndef INTEROP_TRANSFORM_HLSLI
#define INTEROP_TRANSFORM_HLSLI

#include "Interop.h"

INTEROP_DATA_STRUCT(Transform, 4)
{
    INTEROP_ROW_MAJOR(float3x4) Transform;
    INTEROP_ROW_MAJOR(float3x4) PrevTransform;
};
VALIDATE_ALIGNMENT(TransformData, 4);

#endif
