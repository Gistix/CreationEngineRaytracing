#ifndef VERTEX_ANIMATION_HLSL
#define VERTEX_ANIMATION_HLSL

#include "Interop.h"
#include "interop/VertexDesc.hlsli"

namespace VertexAnimation
{
    namespace Flags
    {
        // Vanilla TREE_ANIM: shift along the vertex normal, weighted by vertex color alpha.
        static const uint32_t TreeAnim = (1 << 0);
        // Rest positions come from the CPU-uploaded dynamic float4 buffer (BSDynamicTriShape morphs)
        // instead of the native vertex buffer.
        static const uint32_t DynamicSource = (1 << 1);
        // Write pre-skin positions to the animated position buffer; Skinning produces the final vertices.
        // Without it the pass writes the final live vertices and previous positions itself.
        static const uint32_t SkinnedOutput = (1 << 2);
    }
}

INTEROP_STRUCT(VertexAnimationData, 4)
{
	// Shared bindless slot for the original (rest-pose), live, prev-position and animated position buffers.
	uint Index;
	// Dynamic float4 source buffer slot (DynamicSource only).
	uint DynamicIndex;
	uint VertexCount;
	uint Flags;
	// Native packed vertex layout of the original buffer (and of the live buffer when not skinned).
	VertexDesc VertexDesc;
	uint Pad0;
	uint Pad1;
	// TreeAnim: x = leaf frequency * wind magnitude, y = leaf amplitude, z = timer, w = unused.
	float4 Params;
};
VALIDATE_ALIGNMENT(VertexAnimationData, 4);
VALIDATE_SIZE(VertexAnimationData, 48);

#endif
