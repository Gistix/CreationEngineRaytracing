#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Transform.hlsli"

ConstantBuffer<CameraData>        Camera           : register(b0);
ConstantBuffer<RaytracingData>    Raytracing       : register(b1);
ConstantBuffer<FeatureData>       Features         : register(b2);

StructuredBuffer<Instance>        Instances        : register(t0);
StructuredBuffer<Mesh>            Meshes           : register(t1);
StructuredBuffer<Transform>       Transforms       : register(t2);
ByteAddressBuffer                 PropertiesBuffer : register(t3);
ByteAddressBuffer                 MeshSlotRemap    : register(t4);

Texture2D<float4>                 WaterFlowMap         : register(t5);
Texture2D<float4>                 WaterDisplacementMap : register(t6);
Texture2D<float4>                 ProjNoiseMap         : register(t7);
Texture2D<float4>                 SkinDetailNormal     : register(t8);

StructuredBuffer<Triangle>        Triangles[]      : register(t0, space1);
ByteAddressBuffer                 Vertices[]       : register(t0, space2);
ByteAddressBuffer                 Materials[]      : register(t0, space3);
Texture2D<float4>                 Textures[]       : register(t0, space4);
TextureCube<float4>               CubeTextures[]   : register(t0, space7);
StructuredBuffer<float4>          DynamicPositions[] : register(t0, space8);

SamplerState                      DefaultSampler   : register(s0);
SamplerState                      ClampSampler     : register(s1);
SamplerState                      PointWrapSampler : register(s2);

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

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

struct VertexOut
{
    float4 Position      : SV_POSITION;
    float3 WorldPosition : POSITION;
    float2 TexCoord      : TEXCOORD;
    float3 Normal        : NORMAL;
    float3 Tangent       : TANGENT;
    float3 Bitangent     : BITANGENT;
    float4 Color         : COLOR0;
    float4 LandBlend0    : COLOR1;
    float4 LandBlend1    : COLOR2;  
    nointerpolation uint MeshIndex : MESHINDEX;
};

VertexOut MainVS(in uint vertexID : SV_VertexID, in uint instanceID : SV_InstanceID)
{
    VertexOut o;

    const uint packed = MeshSlotRemap.Load(instanceID * 4u);
    const uint geometrySlot = packed & 0xFFFFu;
    const uint instanceIndex = packed >> 16;

    Instance instance = Instances[NonUniformResourceIndex(instanceIndex)];

    Mesh mesh = Meshes[NonUniformResourceIndex(geometrySlot)];
    uint meshSlot = mesh.MeshID;
    Transform meshTransform = Transforms[NonUniformResourceIndex(meshSlot)];

    Properties props = PropertiesBuffer.Load<Properties>(meshSlot * sizeof(Properties));
    
    uint triangleID = vertexID / 3;
    uint vertexInTriangle = vertexID % 3;

    const uint safePrimitiveIndex = min(triangleID, (uint)mesh.NumTriangles);
    Triangle tri = Triangles[NonUniformResourceIndex(mesh.IndexID)][mesh.TriangleOffset + safePrimitiveIndex];
    const uint16_t triVerts[3] = { tri.x, tri.y, tri.z };
    uint16_t triVertex = triVerts[vertexInTriangle];

    const bool isMSN = props.ShaderFlags & ShaderFlags::kModelSpaceNormals;
    ByteAddressBuffer vertices = Vertices[NonUniformResourceIndex(mesh.VertexID)];
    Vertex vertex = GetVertex(vertices, mesh.VertexDesc, triVertex, isMSN, mesh.NumVertices);

    // Position-less dynamic meshes (BSDynamicTriShape) keep positions in a live float4 buffer.
    if (mesh.Type == MeshType::Dynamic && !mesh.VertexDesc.HasFlag(VertexFlags::Vertex))
    {
        StructuredBuffer<float4> dynPos = DynamicPositions[NonUniformResourceIndex(mesh.DynamicID)];
        vertex.Position = dynPos[NonUniformResourceIndex(triVertex)].xyz;
    }

    float3 rootSpacePosition = mul(meshTransform.Transform, float4(vertex.Position, 1.0f));
    float3 worldSpacePosition = mul(instance.Transform, float4(rootSpacePosition, 1.0f));

    float4 clipSpacePosition = mul(Camera.ViewProj, float4(worldSpacePosition - Camera.Position, 1.0));

    float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) meshTransform.Transform);
    
    o.Position = clipSpacePosition;
    o.WorldPosition = worldSpacePosition;
    o.TexCoord = vertex.Texcoord0;
    o.Normal = normalize(mul(objectToWorld3x3, vertex.Normal));
    o.Bitangent = normalize(mul(objectToWorld3x3, vertex.Bitangent));
    o.Tangent = normalize(mul(objectToWorld3x3, vertex.Tangent));
    o.Color = vertex.Color.unpack();
    o.LandBlend0 = vertex.LandBlend0.unpack();
    o.LandBlend1 = vertex.LandBlend1.unpack();    
    o.MeshIndex = meshSlot;
    
    return o;
}

struct PixelOut
{
	float4 Albedo           : SV_TARGET0;
 	float4 NormalRoughness  : SV_TARGET1;
 	float4 EmissiveMetallic : SV_TARGET2;
};

PixelOut MainPS(in VertexOut i)
{
    PixelOut o;
    
    Mesh mesh = Meshes[i.MeshIndex];
    Properties props = PropertiesBuffer.Load<Properties>(i.MeshIndex * sizeof(Properties));
 
    Surface surface = SurfaceMaker::make(i.WorldPosition, i.TexCoord, i.Normal, i.Tangent, i.Bitangent, i.Color, i.LandBlend0, i.LandBlend1, mesh, props);
    
    if (props.AlphaFlags & AlphaFlags::Test && surface.Alpha < props.AlphaThreshold)
        discard;
    
    o.Albedo = float4(surface.Albedo, 1.0f);
    o.NormalRoughness = float4(surface.Normal * 0.5f + 0.5f, surface.Roughness);
    o.EmissiveMetallic = float4(surface.Emissive, surface.Metallic);
    
    return o;
}
