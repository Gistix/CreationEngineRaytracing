#ifndef BONE_MATRIX_HLSL
#define BONE_MATRIX_HLSL

#include "Interop.h"

INTEROP_STRUCT(BoneMatrix, 16)
{
	INTEROP_ROW_MAJOR(float3x4) World;
};
VALIDATE_ALIGNMENT(BoneMatrix, 16);

#endif