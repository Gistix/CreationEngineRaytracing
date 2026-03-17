#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

#include "Interop.h"

struct VertexUpdateData
{
	uint index;
	uint updateFlags;
	uint vertexCount;
    uint boneOffset;
	uint shapeFlags;
	uint3 pad0;	
};
VALIDATE_ALIGNMENT(VertexUpdateData, 4);

#endif