#ifndef GEOMETRY_HLSL
#define GEOMETRY_HLSL

#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/SIA.hlsli"

#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Transform.hlsli"
#include "interop/Properties.hlsli"
#include "interop/MeshSlotRemap.hlsli"

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

inline float4 InterpolateQuaternion(half4 u, half4 v, half4 w, float3 bary)
{
    float4 q0 = (float4)u;
    float4 q1 = (float4)v;
    float4 q2 = (float4)w;

    // hemisphere alignment
    if (dot(q0, q1) < 0.0) q1 = -q1;
    if (dot(q0, q2) < 0.0) q2 = -q2;

    float4 q =
        q0 * bary.x +
        q1 * bary.y +
        q2 * bary.z;

    return normalize(q);
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

// Reads the geometry slot from the indirection buffer: entry.x = geometrySlot, entry.y = instanceID
// Meshes[geometrySlot] has the per-geometry MeshData, access .MeshSlot for transforms/properties.
uint2 GetMeshRemapEntry(in Instance instance, uint geometryIndex)
{
    uint remapIdx = GetSafeMeshIndex(instance, geometryIndex);
    return MeshSlotRemap.Load2(remapIdx * MESH_SLOT_REMAP_STRIDE);
}

uint GetMeshSlot(in Instance instance, uint geometryIndex)
{
    uint2 entry = GetMeshRemapEntry(instance, geometryIndex);
    return Meshes[NonUniformResourceIndex(entry.x)].MeshSlot;
}

Properties GetMeshProperties(uint meshSlot)
{
    return PropertiesBuffer.Load<Properties>(meshSlot * sizeof(Properties));
}

Mesh GetMesh(in uint instanceIndex, in uint geometryIndex)
{
    Instance instance = GetInstance(instanceIndex);
    uint2 entry = GetMeshRemapEntry(instance, geometryIndex);
    return Meshes[NonUniformResourceIndex(entry.x)];
}

Mesh GetMesh(in uint instanceIndex, in uint geometryIndex, out Instance instance)
{
    instance = GetInstance(instanceIndex);
    uint2 entry = GetMeshRemapEntry(instance, geometryIndex);
    return Meshes[NonUniformResourceIndex(entry.x)];
}

Mesh GetMesh(in Payload payload, out Instance instance)
{
    instance = GetInstance(payload.GetInstanceIndex());
    uint2 entry = GetMeshRemapEntry(instance, payload.GetGeometryIndex());
    return Meshes[NonUniformResourceIndex(entry.x)];
}

Properties GetMeshProperties(in Payload payload)
{
    Instance instance = GetInstance(payload.GetInstanceIndex());
    uint2 entry = GetMeshRemapEntry(instance, payload.GetGeometryIndex());
    return GetMeshProperties(Meshes[NonUniformResourceIndex(entry.x)].MeshSlot);
}

uint GetMeshSlot(in Payload payload)
{
    Instance instance = GetInstance(payload.GetInstanceIndex());
    uint2 entry = GetMeshRemapEntry(instance, payload.GetGeometryIndex());
    return Meshes[NonUniformResourceIndex(entry.x)].MeshSlot;
}

Transform GetTransform(in uint meshIndex)
{
    return Transforms[NonUniformResourceIndex(meshIndex)];
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

Vertex GetVertex(ByteAddressBuffer vertices, VertexDesc vertexDesc, uint index, bool isMSN, uint numVertices)
{
    Vertex vertex = (Vertex)0;

    const uint vertexSize = (uint)vertexDesc.GetVertexSize();
    
    // Cast to 32-bit before multiplying: GetVertexSize() and index are uint16_t, so a 16-bit
    // multiply would overflow (e.g. stride 32 * index 2048 = 0) and corrupt high-index vertices.
    const uint vertexOffset = vertexSize * index;

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
            vertex.Bitangent = (half3)B;
            
            float3 T = float3(pos.w, normal.w, bitangent.w);
            T = normalize(T - N * dot(N, T));
            vertex.Tangent = (half3)T;
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

    if (isMSN)
    {
        const uint quatOffset = (vertexSize * numVertices) + index * 8u;
        const uint2 packed = vertices.Load2(quatOffset);
        
        half4 q;
        q.x = (half)f16tof32(packed.x & 0xffff);
        q.y = (half)f16tof32(packed.x >> 16);
        q.z = (half)f16tof32(packed.y & 0xffff);
        q.w = (half)f16tof32(packed.y >> 16);
        
        vertex.Normal = q.xyz;
        vertex.Tangent.x = q.w;
    }
    
    return vertex;
}

void GetVertices(in Mesh mesh, in Properties meshProps, in uint primitiveIndex, out Vertex v0, out Vertex v1, out Vertex v2)
{
    const uint safePrimitiveIndex = min(primitiveIndex, mesh.NumTriangles);
    
    const Triangle geomTriangle = GetTriangle(mesh.IndexID, mesh.IndexOffset + safePrimitiveIndex);

    const bool isMSN = meshProps.ShaderFlags & ShaderFlags::kModelSpaceNormals;
    
    const ByteAddressBuffer vertices = Vertices[NonUniformResourceIndex(mesh.VertexID)];
    v0 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.x, isMSN, mesh.NumVertices);
    v1 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.y, isMSN, mesh.NumVertices);
    v2 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.z, isMSN, mesh.NumVertices);

    // Position-less dynamic meshes (BSDynamicTriShape) keep positions in the live float4 buffer,
    // not in the byte-address vertex buffer. Reconstruct them so flat normals / object-space pos are valid.
    if (mesh.Type == MeshType::Dynamic && !mesh.VertexDesc.HasFlag(VertexFlags::Vertex))
    {
        StructuredBuffer<float4> dynPos = DynamicPositions[NonUniformResourceIndex(mesh.DynamicID)];
        v0.Position = dynPos[NonUniformResourceIndex(geomTriangle.x)].xyz;
        v1.Position = dynPos[NonUniformResourceIndex(geomTriangle.y)].xyz;
        v2.Position = dynPos[NonUniformResourceIndex(geomTriangle.z)].xyz;
    }
}

#if defined(HAS_PREV_POSITIONS)
void GetVertices(in Mesh mesh, in Properties meshProps, in uint primitiveIndex, out Vertex v0, out Vertex v1, out Vertex v2, out float3 prevPos0, out float3 prevPos1, out float3 prevPos2)
{
    const uint safePrimitiveIndex = min(primitiveIndex, mesh.NumTriangles);

    Triangle geomTriangle = GetTriangle(mesh.IndexID, mesh.IndexOffset + safePrimitiveIndex);

    const bool isMSN = meshProps.ShaderFlags & ShaderFlags::kModelSpaceNormals;

    ByteAddressBuffer vertices = Vertices[NonUniformResourceIndex(mesh.VertexID)];
    v0 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.x, isMSN, mesh.NumVertices);
    v1 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.y, isMSN, mesh.NumVertices);
    v2 = GetVertex(vertices, mesh.VertexDesc, geomTriangle.z, isMSN, mesh.NumVertices);

    if (mesh.Type == MeshType::Dynamic && !mesh.VertexDesc.HasFlag(VertexFlags::Vertex))
    {
        StructuredBuffer<float4> dynPos = DynamicPositions[NonUniformResourceIndex(mesh.DynamicID)];
        v0.Position = dynPos[NonUniformResourceIndex(geomTriangle.x)].xyz;
        v1.Position = dynPos[NonUniformResourceIndex(geomTriangle.y)].xyz;
        v2.Position = dynPos[NonUniformResourceIndex(geomTriangle.z)].xyz;

        // Previous-frame positions are stored immediately after the current ones.
        const uint prevBase = mesh.NumVertices;
        prevPos0 = dynPos[NonUniformResourceIndex(prevBase + geomTriangle.x)].xyz;
        prevPos1 = dynPos[NonUniformResourceIndex(prevBase + geomTriangle.y)].xyz;
        prevPos2 = dynPos[NonUniformResourceIndex(prevBase + geomTriangle.z)].xyz;
    }
    else
    {
        StructuredBuffer<float3> prevVertices = PrevPositions[NonUniformResourceIndex(mesh.VertexID)];
        prevPos0 = prevVertices[NonUniformResourceIndex(geomTriangle.x)];
        prevPos1 = prevVertices[NonUniformResourceIndex(geomTriangle.y)];
        prevPos2 = prevVertices[NonUniformResourceIndex(geomTriangle.z)];
    }
}
#endif

#endif // GEOMETRY_HLSL
