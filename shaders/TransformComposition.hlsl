#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Transform.hlsli"
#include "interop/RowMajorFloat3x4.hlsli"
#include "interop/MeshSlotRemap.hlsli"

float4x4 ToFloat4x4(float3x4 m)
{
    return float4x4(
        m[0], m[1], m[2], 
        float4(0, 0, 0, 1)
    );
}

// Fast inverse for a 3x3 matrix using the cofactor/cross-product trick.
// Works for any invertible 3x3 (rotation + non-uniform scale + shear),
// not just orthonormal rotations.
float3x3 Inverse3x3(float3x3 m)
{
    float3 row0 = m[0];
    float3 row1 = m[1];
    float3 row2 = m[2];

    float3 minor0 = cross(row1, row2);
    float3 minor1 = cross(row2, row0);
    float3 minor2 = cross(row0, row1);

    float det = dot(row0, minor0);

    float3x3 adjT;
    adjT[0] = minor0;
    adjT[1] = minor1;
    adjT[2] = minor2;

    return transpose(adjT) / det;
}

// Inverts a 4x4 affine transform (rotation/scale/shear + translation,
// bottom row assumed to be (0,0,0,1)) far more cheaply than the general
// 4x4 cofactor expansion.
float4x4 InverseAffine(row_major float4x4 m)
{
    float3x3 R = float3x3(
        m[0].xyz,
        m[1].xyz,
        m[2].xyz
    );

    float3x3 Rinv = Inverse3x3(R);

    float3 t = float3(m[0].w, m[1].w, m[2].w);
    float3 tInv = -mul(Rinv, t);

    float4x4 result;
    result[0] = float4(Rinv[0], tInv.x);
    result[1] = float4(Rinv[1], tInv.y);
    result[2] = float4(Rinv[2], tInv.z);
    result[3] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return result;
}

StructuredBuffer<Mesh>        Meshes          : register(t0);
StructuredBuffer<Instance>    Instances       : register(t1);
StructuredBuffer<RowMajorFloat3x4> CurrentTransforms : register(t2);
StructuredBuffer<RowMajorFloat3x4> PrevTransforms : register(t3);
ByteAddressBuffer             MeshSlotRemap   : register(t4);
struct PushConstants
{
    uint NumMeshes;
};

ConstantBuffer<PushConstants> PC : register(b0);

RWStructuredBuffer<Transform> TransformsOut    : register(u0);

[numthreads(64, 1, 1)]
void Main(uint3 DTid : SV_DispatchThreadID)
{
    uint remapIdx = DTid.x;

    if (remapIdx >= PC.NumMeshes)
        return;

    // Read remap entry: {geometrySlot, instanceID}
    uint2 entry = MeshSlotRemap.Load2(remapIdx * MESH_SLOT_REMAP_STRIDE);
    const uint geometryID = entry.x;
    const uint instanceID = entry.y;

    Mesh mesh = Meshes[geometryID];
    const uint meshID = mesh.MeshID;
    Instance instance = Instances[instanceID];

    // meshSlot == transform index
    float3x4 transform = CurrentTransforms[meshID].Value;
    float3x4 prevTransform = PrevTransforms[meshID].Value;

    float4x4 localMat = mul(InverseAffine(ToFloat4x4(instance.Transform)), ToFloat4x4(transform));
    float4x4 prevLocalMat = mul(InverseAffine(ToFloat4x4(instance.PrevTransform)), ToFloat4x4(prevTransform));

    Transform local;
    local.Transform = float3x4(
        localMat[0], 
        localMat[1], 
        localMat[2]
    );  
    local.PrevTransform = float3x4(
        prevLocalMat[0], 
        prevLocalMat[1], 
        prevLocalMat[2]
    ); 
    TransformsOut[meshID] = local;
}
