#ifndef ROW_MAJOR_FLOAT3X4_HLSL
#define ROW_MAJOR_FLOAT3X4_HLSL

#include "Interop.h"

INTEROP_STRUCT(RowMajorFloat3x4, 16)
{
	INTEROP_ROW_MAJOR(float3x4) Value;
};
VALIDATE_ALIGNMENT(RowMajorFloat3x4, 16);

#endif
