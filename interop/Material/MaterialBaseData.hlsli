#ifndef MATERIAL_BASE_DATA_HLSL
#define MATERIAL_BASE_DATA_HLSL

#include "Interop.h"

INTEROP_STRUCT(MaterialBaseData, 4)
{
    half2 TexCoordOffset;
    half2 TexCoordScale;
};
VALIDATE_ALIGNMENT(MaterialBaseData, 4);

#endif // MATERIAL_BASE_DATA_HLSL