#ifndef VERTEX_ANIMATION_HLSL
#define VERTEX_ANIMATION_HLSL

#include "Interop.h"
#include "interop/VertexDesc.hlsli"
#include "interop/Transform.hlsli"

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

// One merged grass group (N instances x V blade vertices). Positions are written in blade (instance-local)
// space so the per-instance transforms in the instance transform pool stay valid for shading.
INTEROP_STRUCT(GrassWindData, 4)
{
	// Original (native) blade vertices in the VertexCopy table.
	uint Index;
	// Output float4 positions in the dynamic vertex table: [current N*V | previous N*V].
	uint DynamicIndex;
	uint VertexCount;
	uint InstanceCount;
	uint InstanceTransformBase;
	uint Pad0;
	// Native packed blade vertex layout.
	VertexDesc VertexDesc;
	// Wind displacement direction (vanilla WindVector.xy, z = 0) taken through the shape-local rotation/scale,
	// i.e. the space the instance transform pool outputs. w = wind strength (WindVector.z).
	float4 Wind;
	// x = WindTimer, y = PreviousWindTimer.
	float4 Timers;
	// Inverse shape-local transform: recovers the instance origin (InstanceData1.xy) from the pool transform.
	INTEROP_ROW_MAJOR(float3x4) InverseShapeLocal;
};
VALIDATE_ALIGNMENT(GrassWindData, 4);
VALIDATE_SIZE(GrassWindData, 112);

#endif
