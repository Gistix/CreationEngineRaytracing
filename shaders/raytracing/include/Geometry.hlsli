#ifndef GEOMETRY_HLSL
#define GEOMETRY_HLSL

#include "raytracing/include/Payload.hlsli"

#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"

#include "raytracing/include/Registers.hlsli"

float3 GetBary(float2 barycentrics)
{
    return float3(
        1.0f - barycentrics.x - barycentrics.y,
        barycentrics.x,
        barycentrics.y
    );
}

inline float Interpolate(float u, float v, float w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float Interpolate(half u, half v, half w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float2 Interpolate(half2 u, half2 v, half2 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float3 Interpolate(float3 u, float3 v, float3 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float3 Interpolate(half3 u, half3 v, half3 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float4 Interpolate(half4 u, half4 v, half4 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

// ============================================================================
// NVIDIA Self-Intersection Avoidance (SIA)
// Based on: https://github.com/NVIDIA/self-intersection-avoidance
// 
// Provides precise barycentric interpolation and tight error-bounded ray offset
// computation. Enable with USE_SIA_INTERPOLATION macro.
// ============================================================================

// Compute precise object-space position, world-space position, geometric normal,
// and a safe spawn offset to prevent self-intersection of secondary rays.
//
// This implements the algorithm from NVIDIA's self-intersection-avoidance library.
// Unlike the standard Interpolate() which uses a weighted sum, this uses precise
// MAD-based interpolation with edge vectors for better floating-point accuracy,
// and computes a tight error bound through the entire transform chain.
//
// Parameters:
//   v0, v1, v2     - Triangle vertex positions in object space
//   bary           - Hit barycentrics (float2: u, v)
//   o2w            - Object-to-world transform (row-major float3x4)
//   w2o            - World-to-object transform (row-major float3x4)
//
// Outputs:
//   outObjPosition - Interpolated position in object space
//   outWldPosition - Interpolated position in world space
//   outObjNormal   - Normalized geometric face normal in object space
//   outWldNormal   - Normalized geometric face normal in world space
//   outWldOffset   - Safe offset distance along world normal to avoid self-intersection
void SIA_SafeSpawnPoint(
    out float3     outObjPosition,
    out float3     outWldPosition,
    out float3     outObjNormal,
    out float3     outWldNormal,
    out float      outWldOffset,
    const float3   v0,
    const float3   v1,
    const float3   v2,
    const float2   bary,
    const float3x4 o2w,
    const float3x4 w2o)
{
    precise float3 edge1 = v1 - v0;
    precise float3 edge2 = v2 - v0;

    // Interpolate triangle using barycentrics.
    // Add in base vertex last to reduce object space error.
    precise float3 objPosition = v0 + mad(bary.x, edge1, mul(bary.y, edge2));
    float3 objNormal = cross(edge1, edge2);

    // Transform object space position with precise MAD chain.
    // Add in translation last to reduce world space error.
    precise float3 wldPosition;
    wldPosition.x = o2w._m03 +
        mad(o2w._m00, objPosition.x,
            mad(o2w._m01, objPosition.y,
                mul(o2w._m02, objPosition.z)));
    wldPosition.y = o2w._m13 +
        mad(o2w._m10, objPosition.x,
            mad(o2w._m11, objPosition.y,
                mul(o2w._m12, objPosition.z)));
    wldPosition.z = o2w._m23 +
        mad(o2w._m20, objPosition.x,
            mad(o2w._m21, objPosition.y,
                mul(o2w._m22, objPosition.z)));

    // Transform normal to world-space using inverse transpose matrix
    float3 wldNormal = mul(transpose((float3x3)w2o), objNormal);

    // Normalize world space normal
    const float wldScale = rsqrt(dot(wldNormal, wldNormal));
    wldNormal = mul(wldScale, wldNormal);

    // Error bound constants
    const float c0 = 5.9604644775390625E-8f;
    const float c1 = 1.788139769587360206060111522674560546875E-7f;

    const float3 extent3 = abs(edge1) + abs(edge2) + abs(edge1 - edge2);
    const float  extent = max(max(extent3.x, extent3.y), extent3.z);

    // Bound object space error due to reconstruction and intersection
    float3 objErr = mad(c0, abs(v0), mul(c1, extent));

    // Bound world space error due to object to world transform
    const float c2 = 1.19209317972490680404007434844970703125E-7f;
    float3 wldErr = mad(c1, mul(abs((float3x3)o2w), abs(objPosition)), mul(c2, abs(transpose(o2w)[3])));

    // Bound object space error due to world to object transform
    objErr = mad(c2, mul(abs(w2o), float4(abs(wldPosition), 1)), objErr);

    // Compute world space self intersection avoidance offset
    float wldOffset = dot(wldErr, abs(wldNormal));
    float objOffset = dot(objErr, abs(objNormal));
    wldOffset = mad(wldScale, objOffset, wldOffset);

    // Output
    outObjPosition = objPosition;
    outWldPosition = wldPosition;
    outObjNormal = normalize(objNormal);
    outWldNormal = wldNormal;
    outWldOffset = wldOffset;
}

// Simplified version when only position, normal, and offset are needed in world space.
// Combines mesh-local and instance transforms into a single o2w chain.
// This version does NOT require the w2o inverse -- it uses the simpler fallback
// that omits the w2o error term (still much better than the standard approach).
void SIA_SafeSpawnPointSimple(
    out float3     outWldPosition,
    out float3     outWldFaceNormal,
    out float      outWldOffset,
    const float3   v0,
    const float3   v1,
    const float3   v2,
    const float2   bary,
    const float3x4 o2w)
{
    precise float3 edge1 = v1 - v0;
    precise float3 edge2 = v2 - v0;

    // Precise interpolation with edge vectors
    precise float3 objPosition = v0 + mad(bary.x, edge1, mul(bary.y, edge2));
    float3 objNormal = cross(edge1, edge2);

    // Precise world-space transform
    precise float3 wldPosition;
    wldPosition.x = o2w._m03 +
        mad(o2w._m00, objPosition.x,
            mad(o2w._m01, objPosition.y,
                mul(o2w._m02, objPosition.z)));
    wldPosition.y = o2w._m13 +
        mad(o2w._m10, objPosition.x,
            mad(o2w._m11, objPosition.y,
                mul(o2w._m12, objPosition.z)));
    wldPosition.z = o2w._m23 +
        mad(o2w._m20, objPosition.x,
            mad(o2w._m21, objPosition.y,
                mul(o2w._m22, objPosition.z)));

    // Transform normal to world space (using o2w directly, which is
    // correct for orthogonal/uniform-scale transforms and approximate otherwise)
    float3 wldNormal = mul((float3x3)o2w, objNormal);
    const float wldScale = rsqrt(dot(wldNormal, wldNormal));
    wldNormal = mul(wldScale, wldNormal);

    // Error bound constants
    const float c0 = 5.9604644775390625E-8f;
    const float c1 = 1.788139769587360206060111522674560546875E-7f;
    const float c2 = 1.19209317972490680404007434844970703125E-7f;

    const float3 extent3 = abs(edge1) + abs(edge2) + abs(edge1 - edge2);
    const float  extent = max(max(extent3.x, extent3.y), extent3.z);

    // Object space error from reconstruction
    float3 objErr = mad(c0, abs(v0), mul(c1, extent));

    // World space error from o2w transform
    float3 wldErr = mad(c1, mul(abs((float3x3)o2w), abs(objPosition)), mul(c2, abs(transpose(o2w)[3])));

    // Compute offset
    float wldOffset = dot(wldErr, abs(wldNormal));
    float objOffset = dot(objErr, abs(objNormal));
    wldOffset = mad(wldScale, objOffset, wldOffset);

    outWldPosition = wldPosition;
    outWldFaceNormal = wldNormal;
    outWldOffset = wldOffset;
}

Instance GetInstance(uint instanceIdx)
{
    return Instances[NonUniformResourceIndex(instanceIdx)];
}

Mesh GetMesh(in uint instanceIndex, in uint geometryIndex)
{
    Instance instance = GetInstance(instanceIndex);
    return Meshes[NonUniformResourceIndex(instance.FirstGeometryID + geometryIndex)];
}

Mesh GetMesh(in uint instanceIndex, in uint geometryIndex, out Instance instance)
{
    instance = GetInstance(instanceIndex);
    return Meshes[NonUniformResourceIndex(instance.FirstGeometryID + geometryIndex)];
}

Mesh GetMesh(in Payload payload, out Instance instance)
{
    instance = GetInstance(payload.InstanceIndex());
    return Meshes[NonUniformResourceIndex(instance.FirstGeometryID + payload.GeometryIndex())];
}

Triangle GetTriangle(in uint shapeIdx, in uint primitiveIdx)
{
    return Triangles[NonUniformResourceIndex(shapeIdx)][primitiveIdx];
}

void GetVertices(in uint meshIndex, in uint primitiveIndex, out Vertex v0, out Vertex v1, out Vertex v2)
{
    Triangle geomTriangle = GetTriangle(meshIndex, primitiveIndex);

    StructuredBuffer<Vertex> vertices = Vertices[NonUniformResourceIndex(meshIndex)];
    v0 = vertices[NonUniformResourceIndex(geomTriangle.x)];
    v1 = vertices[NonUniformResourceIndex(geomTriangle.y)];
    v2 = vertices[NonUniformResourceIndex(geomTriangle.z)];
}

#endif // GEOMETRY_HLSL