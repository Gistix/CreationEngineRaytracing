#ifndef GEOMETRY_HLSL
#define GEOMETRY_HLSL

#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/SIA.hlsli"

#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"

#include "interop/Vertex.hlsli"

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

Instance GetInstance(uint instanceIdx)
{
    const uint safeInstanceIndex = min(instanceIdx, Raytracing.NumInstances);
    return Instances[NonUniformResourceIndex(safeInstanceIndex)];
}

uint GetSafeMeshIndex(in Instance instance, uint geometryIndex)
{
    const uint safeGeometryIndex = min(geometryIndex, instance.NumGeometry);
    return min(instance.FirstGeometryID + safeGeometryIndex, Raytracing.NumMeshes);
}

Mesh GetMesh(in uint instanceIndex, in uint geometryIndex)
{
    Instance instance = GetInstance(instanceIndex);
    return Meshes[NonUniformResourceIndex(GetSafeMeshIndex(instance, geometryIndex))];
}

Mesh GetMesh(in uint instanceIndex, in uint geometryIndex, out Instance instance)
{
    instance = GetInstance(instanceIndex);
    return Meshes[NonUniformResourceIndex(GetSafeMeshIndex(instance, geometryIndex))];
}

Mesh GetMesh(in Payload payload, out Instance instance)
{
    instance = GetInstance(payload.GetInstanceIndex());
    return Meshes[NonUniformResourceIndex(GetSafeMeshIndex(instance, payload.GetGeometryIndex()))];
}

Triangle GetTriangle(in uint meshIndex, in uint primitiveIdx)
{
    return Triangles[NonUniformResourceIndex(meshIndex)][primitiveIdx];
}

// Decodes a signed-normalized byte4 (ubyte4 * 2 - 1) from a raw uint.
inline float4 UnpackByte4SNorm(uint packed)
{
    const float4 v = float4(
        (float)((packed >>  0) & 0xFF),
        (float)((packed >>  8) & 0xFF),
        (float)((packed >> 16) & 0xFF),
        (float)((packed >> 24) & 0xFF));
    return v * (1.0f / 255.0f) * 2.0f - 1.0f;
}

Vertex GetVertex(ByteAddressBuffer vertices, VertexDesc vertexDesc, uint16_t index)
{
    Vertex vertex = (Vertex)0;

    const uint vertexOffset = vertexDesc.GetVertexSize() * index;

    float4 pos = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 normal = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 bitangent = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // Position (float4; w carries tangent.x)
    if (vertexDesc.HasFlag(VertexFlags::Vertex))
    {
        const uint offset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Position);
        pos = asfloat(vertices.Load4(offset));
        vertex.Position = pos.xyz;
    }

    // Texcoord0 (half2)
    if (vertexDesc.HasFlag(VertexFlags::UV))
    {
        const uint offset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Texcoord0);
        const uint packed = vertices.Load(offset);
        vertex.Texcoord0 = half2(f16tof32(packed & 0xFFFF), f16tof32(packed >> 16));
    }

    // Normal (byte4 snorm; w carries tangent.y)
    if (vertexDesc.HasFlag(VertexFlags::Normal))
    {
        const uint offset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Normal);
        normal = UnpackByte4SNorm(vertices.Load(offset));

        const float3 N = normalize(normal.xyz);
        vertex.Normal = (half3)N;

        // Tangent (reconstructed from the binormal attribute; w carries tangent.z)
        if (vertexDesc.HasFlag(VertexFlags::Tangent))
        {
            const uint tangOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Binormal);
            bitangent = UnpackByte4SNorm(vertices.Load(tangOffset));

            float3 B = bitangent.xyz;
            B = normalize(B - N * dot(N, B));

            float3 T = float3(pos.w, normal.w, bitangent.w);
            T = normalize(T - N * dot(N, T));

            vertex.Tangent = (half3)normalize(T);
            vertex.Handedness = -(dot(cross(N, T), B) < 0.0f ? -1.0f : 1.0f);
        }
    }

    // Vertex color
    if (vertexDesc.HasFlag(VertexFlags::Colors))
    {
        const uint offset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Color);
        const uint packed = vertices.Load(offset);
        vertex.Color.x = (packed >> 0) & 0xFF;
        vertex.Color.y = (packed >> 8) & 0xFF;
        vertex.Color.z = (packed >> 16) & 0xFF;
        vertex.Color.w = (packed >> 24) & 0xFF;
    }

    // Landscape blend data (two packed uints)
    if (vertexDesc.HasFlag(VertexFlags::LandData))
    {
        const uint offset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::LandData);
        const uint packed0 = vertices.Load(offset);
        const uint packed1 = vertices.Load(offset + 4);

        vertex.LandBlend0.x = (packed0 >> 0) & 0xFF;
        vertex.LandBlend0.y = (packed0 >> 8) & 0xFF;
        vertex.LandBlend0.z = (packed0 >> 16) & 0xFF;
        vertex.LandBlend0.w = (packed0 >> 24) & 0xFF;

        vertex.LandBlend1.x = (packed1 >> 0) & 0xFF;
        vertex.LandBlend1.y = (packed1 >> 8) & 0xFF;
        vertex.LandBlend1.z = (packed1 >> 16) & 0xFF;
        vertex.LandBlend1.w = (packed1 >> 24) & 0xFF;
    }

    return vertex;
}

void GetVertices(in Mesh mesh, in uint primitiveIndex, out Vertex v0, out Vertex v1, out Vertex v2)
{
    const uint safePrimitiveIndex = min(primitiveIndex, mesh.NumTriangles);
    
    const Triangle geomTriangle = GetTriangle(mesh.IndexID, safePrimitiveIndex);

    const ByteAddressBuffer vertices = Vertices[NonUniformResourceIndex(mesh.VertexID)];
    v0 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.x);
    v1 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.y);
    v2 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.z);
}

/*Material GetMaterial(in uint meshIndex)
{
    return Materials[NonUniformResourceIndex(meshIndex)][0];
}*/

#if defined(HAS_PREV_POSITIONS)
void GetVertices(in Mesh mesh, in uint primitiveIndex, out Vertex v0, out Vertex v1, out Vertex v2, out float3 prevPos0, out float3 prevPos1, out float3 prevPos2)
{
    const uint meshIndex = mesh.GeometryID;
    const uint safePrimitiveIndex = min(primitiveIndex, mesh.NumTriangles);

    Triangle geomTriangle = GetTriangle(meshIndex, safePrimitiveIndex);

    StructuredBuffer<Vertex> vertices = Vertices[NonUniformResourceIndex(meshIndex)];
    v0 = vertices[NonUniformResourceIndex(geomTriangle.x)];
    v1 = vertices[NonUniformResourceIndex(geomTriangle.y)];
    v2 = vertices[NonUniformResourceIndex(geomTriangle.z)];
    
    StructuredBuffer<float3> prevVertices = PrevPositions[NonUniformResourceIndex(meshIndex)];
    prevPos0 = prevVertices[NonUniformResourceIndex(geomTriangle.x)];
    prevPos1 = prevVertices[NonUniformResourceIndex(geomTriangle.y)];
    prevPos2 = prevVertices[NonUniformResourceIndex(geomTriangle.z)];    
}
#endif

#endif // GEOMETRY_HLSL