#ifndef BONE_TRANSFORM_HLSL
#define BONE_TRANSFORM_HLSL

#include "Interop.h"

// Raw NiTransform packed as float3x4 affine matrix:
//   Rows 0-2: scaled rotation (rotate * scale)
//   Column 3 (w): translation
// Equivalent to the full affine transform of a NiTransform.
INTEROP_STRUCT(BoneTransform, 16)
{
	INTEROP_ROW_MAJOR(float3x4) Affine;
};
VALIDATE_ALIGNMENT(BoneTransform, 16);

// Per-mesh header for the bone compute pass. One per mesh that needs skinning.
INTEROP_STRUCT(MeshBoneHeader, 16)
{
	uint BoneCount;
	uint BoneWorldOffset;
	uint SkinToBoneOffset;
	uint Pad;
	// Geometry-world-inverse packed as float3x4. 48 bytes; the 16-byte alignment
	// bump from the uints above is handled by placing it after Pad.
	INTEROP_ROW_MAJOR(float3x4) GeometryWorldInverse;
};
VALIDATE_ALIGNMENT(MeshBoneHeader, 16);

#endif
