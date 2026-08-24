#include "Interop/RowMajorFloat3x4.hlsli"
#include "Interop/BoneTransform.hlsli"

StructuredBuffer<NiTransformPacked> BoneWorlds       : register(t0);
StructuredBuffer<NiTransformPacked> SkinToBones      : register(t1);
StructuredBuffer<MeshBoneHeader> MeshHeaders         : register(t2);
RWStructuredBuffer<RowMajorFloat3x4> BoneMatricesOut       : register(u0);

float3x4 NiToAffine(NiTransformPacked t)
{
	float3x4 m;
	float s = t.Rot0_Scale.w;
	m[0] = float4(t.Rot0_Scale.xyz * s, t.Translate.x);
	m[1] = float4(t.Rot1.xyz * s, t.Translate.y);
	m[2] = float4(t.Rot2.xyz * s, t.Translate.z);
	return m;
}

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

	const float3x4 boneWorld = NiToAffine(BoneWorlds[header.BoneWorldOffset + globalBoneIndex]);
	const float3x4 skinToBone = NiToAffine(SkinToBones[header.SkinToBoneOffset + globalBoneIndex]);

	NiTransformPacked geomInvPacked;
	geomInvPacked.Rot0_Scale = header.GeomInv_Rot0_Scale;
	geomInvPacked.Rot1      = header.GeomInv_Rot1;
	geomInvPacked.Rot2      = header.GeomInv_Rot2;
	geomInvPacked.Translate = header.GeomInv_Translate;
	const float3x4 geomInv = NiToAffine(geomInvPacked);

	float3x4 m = MulAffine(MulAffine(geomInv, boneWorld), skinToBone);

	BoneMatricesOut[header.BoneWorldOffset + globalBoneIndex].Value = m;
}
