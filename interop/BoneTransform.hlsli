#ifndef BONE_TRANSFORM_HLSL
#define BONE_TRANSFORM_HLSL

#include "Interop.h"

// Packed NiTransform (52 bytes → 64 bytes with padding):
//   Rot0_Scale: rotation row 0 (xyz), scale in w
//   Rot1:       rotation row 1 (xyz), pad in w
//   Rot2:       rotation row 2 (xyz), pad in w
//   Translate:  translation (xyz), pad in w
// Matches CPU-side memory layout: 4 x float4 = 64 bytes, alignas(16).
INTEROP_STRUCT(NiTransformPacked, 16)
{
	float4 Rot0_Scale;
	float4 Rot1;
	float4 Rot2;
	float4 Translate;
};
VALIDATE_ALIGNMENT(NiTransformPacked, 16);

// Per-mesh header for the bone compute pass. One per mesh that needs skinning.
INTEROP_STRUCT(MeshBoneHeader, 16)
{
	uint BoneCount;
	uint BoneWorldOffset;
	uint SkinToBoneOffset;
	uint Pad;
	// Geometry-world-inverse packed as NiTransformPacked (64 bytes).
	float4 GeomInv_Rot0_Scale;
	float4 GeomInv_Rot1;
	float4 GeomInv_Rot2;
	float4 GeomInv_Translate;
};
VALIDATE_ALIGNMENT(MeshBoneHeader, 16);

#endif
