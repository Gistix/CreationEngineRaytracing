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

struct DrawConstants
{
    uint DrawIndex;
};
ConstantBuffer<DrawConstants>     Draw             : register(b3);

StructuredBuffer<Instance>        Instances        : register(t0);
StructuredBuffer<Mesh>            Meshes           : register(t1);
StructuredBuffer<Transform>       Transforms       : register(t2);
ByteAddressBuffer                 PropertiesBuffer : register(t3);
ByteAddressBuffer                 MeshSlotRemap    : register(t4);

Texture2D<float4>                 WaterFlowMap         : register(t5);
Texture2D<float4>                 WaterDisplacementMap : register(t6);
Texture2D<float4>                 ProjNoiseMap         : register(t7);
Texture2D<float4>                 SkinDetailNormal     : register(t8);

ByteAddressBuffer                 Indices[]        : register(t0, space1);
ByteAddressBuffer                 Vertices[]       : register(t0, space2);
ByteAddressBuffer                 Materials[]      : register(t0, space3);
Texture2D<float4>                 Textures[]       : register(t0, space4);
TextureCube<float4>               CubeTextures[]   : register(t0, space7);
StructuredBuffer<float4>          DynamicPositions[] : register(t0, space8);
StructuredBuffer<float3>          PrevPositions[]    : register(t0, space6);

SamplerState                      DefaultSampler   : register(s0);
SamplerState                      ClampSampler     : register(s1);
SamplerState                      PointWrapSampler : register(s2);

#include "include/Surface.hlsli"
#include "raytracing/include/Geometry.hlsli"
#include "include/SurfaceMaker.hlsli"



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
    nointerpolation uint GeometrySlot : GEOMETRYSLOT;
    nointerpolation uint MeshIndex : MESHINDEX;
    float4 CurrentClip  : CURRCLIP;
    float4 PreviousClip : PREVCLIP;
};

VertexOut MainVS(in uint vertexID : SV_VertexID)
{
    VertexOut o;

    const uint packed = MeshSlotRemap.Load(Draw.DrawIndex * 4u);
    const uint geometrySlot = packed & 0xFFFFu;
    const uint instanceIndex = packed >> 16;

    Instance instance = Instances[NonUniformResourceIndex(instanceIndex)];

    Mesh mesh = Meshes[NonUniformResourceIndex(geometrySlot)];
    const uint meshSlot = mesh.MeshID;
    Transform meshTransform = Transforms[NonUniformResourceIndex(meshSlot)];

    Properties props = PropertiesBuffer.Load<Properties>(meshSlot * sizeof(Properties));
    
    const uint triangleID = vertexID / 3;
    const uint vertexInTriangle = vertexID % 3;

    const uint safePrimitiveIndex = min(triangleID, (uint)max(0, (int)mesh.NumTriangles - 1));
    Triangle tri = GetTriangle(mesh.IndexID, mesh.IndexOffset, safePrimitiveIndex);
    const uint16_t triVerts[3] = { tri.x, tri.y, tri.z };
    uint16_t triVertex = triVerts[vertexInTriangle];

    const bool isMSN = (props.ShaderFlags & ShaderFlags::kModelSpaceNormals) != 0;
    ByteAddressBuffer vertices = Vertices[NonUniformResourceIndex(mesh.VertexID)];
    Vertex vertex = GetVertex(vertices, mesh.VertexDesc, mesh.VertexOffset, triVertex, isMSN, mesh.NumVertices);

    // Position-less dynamic meshes (BSDynamicTriShape) keep positions in a live float4 buffer.
    StructuredBuffer<float4> dynPos = DynamicPositions[NonUniformResourceIndex(mesh.DynamicID)];
    if (mesh.Type == MeshType::Dynamic && !mesh.VertexDesc.HasFlag(VertexFlags::Vertex))
    {
        vertex.Position = dynPos[NonUniformResourceIndex(triVertex)].xyz;
    }

    // Previous-frame vertex position (mesh-local space) for motion vectors.
    // Mirrors Geometry.hlsli: skinned meshes and position-full dynamics read the prev-position
    // buffer (written by the skinning pass); position-less dynamics keep the previous set
    // immediately after the current one; static meshes reuse the current position (PrevTransforms
    // capture object motion).
    float3 prevVertexPosition = vertex.Position;
    if (mesh.Type == MeshType::Dynamic && !mesh.VertexDesc.HasFlag(VertexFlags::Vertex))
        prevVertexPosition = dynPos[NonUniformResourceIndex(triVertex + mesh.NumVertices)].xyz;
    else if (mesh.Type == MeshType::Skinned || mesh.Type == MeshType::Dynamic)
        prevVertexPosition = PrevPositions[NonUniformResourceIndex(mesh.VertexID)][triVertex];

    float3 rootSpacePosition = mul(meshTransform.Transform, float4(vertex.Position, 1.0f));
    float3 worldSpacePosition = mul(instance.Transform, float4(rootSpacePosition, 1.0f));

    float4 clipSpacePosition = mul(Camera.ViewProj, float4(worldSpacePosition - Camera.Position, 1.0));

    float3 prevRootSpacePosition = mul(meshTransform.PrevTransform, float4(prevVertexPosition, 1.0f));
    float3 prevWorldSpacePosition = mul(instance.PrevTransform, float4(prevRootSpacePosition, 1.0f));
    float4 prevClipSpacePosition = mul(Camera.PrevViewProj, float4(prevWorldSpacePosition - Camera.PositionPrev, 1.0));

    float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) meshTransform.Transform);
    
    o.Position = clipSpacePosition;
    o.WorldPosition = worldSpacePosition;
    o.TexCoord = vertex.Texcoord0;
    
    const VertexDesc vertexDesc = mesh.VertexDesc;
    if (vertexDesc.HasFlag(VertexFlags::Normal))
    {
        o.Normal = normalize(mul(objectToWorld3x3, vertex.Normal));
        o.Tangent = normalize(mul(objectToWorld3x3, vertex.Tangent));
        o.Bitangent = normalize(mul(objectToWorld3x3, vertex.Bitangent));
    }
    else
    {
        o.Normal = float3(0.0f, 0.0f, 1.0f);
        o.Tangent = float3(0.0f, 1.0f, 0.0f);
        o.Bitangent = float3(1.0f, 0.0f, 0.0f);
    }
    
    if (vertexDesc.HasFlag(VertexFlags::Colors))
        o.Color = vertex.Color.unpack();
    else
        o.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    if (vertexDesc.HasFlag(VertexFlags::LandData))
    {
        o.LandBlend0 = vertex.LandBlend0.unpack();
        o.LandBlend1 = vertex.LandBlend1.unpack();
    }
    else
    {
        o.LandBlend0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        o.LandBlend1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    o.GeometrySlot = geometrySlot;
    o.MeshIndex = meshSlot;

    o.CurrentClip = clipSpacePosition;
    o.PreviousClip = prevClipSpacePosition;
    
    return o;
}

struct PixelOut
{
 	float2 MotionVectors    : SV_TARGET0;   
	float4 Albedo           : SV_TARGET1;
 	float4 NormalRoughness  : SV_TARGET2;
 	float4 EmissiveMetallic : SV_TARGET3;
};

PixelOut MainPS(in VertexOut i)
{
    PixelOut o;
    
    Mesh mesh = Meshes[NonUniformResourceIndex(i.GeometrySlot)];
    Properties props = PropertiesBuffer.Load<Properties>(i.MeshIndex * sizeof(Properties));
 
    Surface surface = SurfaceMaker::make(i.WorldPosition, i.TexCoord, i.Normal, i.Tangent, i.Bitangent, i.Color, i.LandBlend0, i.LandBlend1, mesh, props);
    
    if (props.AlphaFlags & AlphaFlags::Test && surface.Material.Alpha < props.AlphaThreshold)
        discard;

    const float2 currNDC = i.CurrentClip.xy / i.CurrentClip.w;
    const float2 prevNDC = i.PreviousClip.xy / i.PreviousClip.w;
 
    o.MotionVectors = (prevNDC - currNDC) * float2(0.5f, -0.5f);
    o.Albedo = float4(surface.Material.Albedo, 1.0f);
    o.NormalRoughness = float4(surface.Geometry.Normal, surface.Material.Roughness);
    o.EmissiveMetallic = float4(surface.Material.Emissive, surface.Material.Metallic);
    
    return o;
}
