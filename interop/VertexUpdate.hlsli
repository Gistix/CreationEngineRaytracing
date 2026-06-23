#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

#include "Interop.h"
#include "interop/VertexDesc.hlsli"

INTEROP_STRUCT(VertexUpdateData, 4)
{
	// Shared bindless slot for the original (rest-pose), live (output) and prev-position buffers.
	uint index;
	// Dynamic float4 position buffer slot (lives in DynamicMesh; 0 for non-dynamic meshes).
	uint dynamicIndex;
	uint updateFlags;
	uint vertexCount;
	uint boneOffset;
	uint shapeFlags;
	uint numMatrices;
	// Native packed vertex layout used to read the original buffer and write the live buffer.
	VertexDesc VertexDesc;
};
VALIDATE_ALIGNMENT(VertexUpdateData, 4);

#endif
