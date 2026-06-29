#include "Interop/BoneMatrix.hlsli"
#include "Interop/BoneTransform.hlsli"

StructuredBuffer<BoneTransform> BoneWorlds          : register(t0);
StructuredBuffer<BoneTransform> SkinToBones         : register(t1);
StructuredBuffer<MeshBoneHeader> MeshHeaders        : register(t2);
RWStructuredBuffer<BoneMatrix> BoneMatricesOut      : register(u0);

// 3x4 affine transform multiply: a * b, assuming both represent affine
// transforms where the implicit 4th row is [0,0,0,1].
float3x4 MulAffine(float3x4 a, float3x4 b)
{
	float3x4 r;
	r[0].xyz = a[0].x * b[0].xyz + a[0].y * b[1].xyz + a[0].z * b[2].xyz;
	r[0].w   = a[0].x * b[0].w   + a[0].y * b[1].w   + a[0].z * b[2].w   + a[0].w;

	r[1].xyz = a[1].x * b[0].xyz + a[1].y * b[1].xyz + a[1].z * b[2].xyz;
	r[1].w   = a[1].x * b[0].w   + a[1].y * b[1].w   + a[1].z * b[2].w   + a[1].w;

	r[2].xyz = a[2].x * b[0].xyz + a[2].y * b[1].xyz + a[2].z * b[2].xyz;
	r[2].w   = a[2].x * b[0].w   + a[2].y * b[1].w   + a[2].z * b[2].w   + a[2].w;

	return r;
}

[numthreads(64, 1, 1)]
void Main(uint3 DTid : SV_DispatchThreadID)
{
	const uint globalBoneIndex = DTid.x;
	const uint meshIndex = DTid.y;

	MeshBoneHeader header = MeshHeaders[meshIndex];
	if (globalBoneIndex >= header.BoneCount)
		return;

	const float3x4 boneWorld = BoneWorlds[header.BoneWorldOffset + globalBoneIndex].Affine;
	const float3x4 skinToBone = SkinToBones[header.SkinToBoneOffset + globalBoneIndex].Affine;
	const float3x4 geomInv = header.GeometryWorldInverse;

	float3x4 m = MulAffine(MulAffine(geomInv, boneWorld), skinToBone);

	BoneMatricesOut[header.BoneWorldOffset + globalBoneIndex].World = m;
}
