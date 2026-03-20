#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

#include "Interop.h"

INTEROP_STRUCT(VertexUpdateData, 4)
{
	uint index;
	uint updateFlags;
	uint vertexCount;
    uint boneOffset;
	uint shapeFlags;
	uint numMatrices;
};
VALIDATE_ALIGNMENT(VertexUpdateData, 4);

#endif