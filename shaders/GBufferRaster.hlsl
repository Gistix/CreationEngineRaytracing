#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"

struct InstanceConstants
{
    uint InstanceIndex;
    uint GeometryIndex;
};

ConstantBuffer<CameraData>        Camera         : register(b0);
ConstantBuffer<RaytracingData>    Raytracing     : register(b1);
ConstantBuffer<FeatureData>       Features       : register(b2);
ConstantBuffer<InstanceConstants> InstanceConst  : register(b3);

StructuredBuffer<Instance>        Instances      : register(t0);
StructuredBuffer<Mesh>            Meshes         : register(t1);

StructuredBuffer<uint16_t3>       Triangles[]    : register(t0, space1);
StructuredBuffer<Vertex>          Vertices[]     : register(t0, space2);
Texture2D<float4>                 Textures[]     : register(t0, space3);

SamplerState                      DefaultSampler : register(s0);

#include "include/Surface.hlsli"
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
    nointerpolation uint MeshIndex : MESHINDEX;
};

VertexOut MainVS(in uint vertexID : SV_VertexID)
{
    VertexOut o;
    
    Instance instance = Instances[InstanceConst.InstanceIndex];

    uint meshIndex = instance.FirstGeometryID + InstanceConst.GeometryIndex;
    
    Mesh mesh = Meshes[meshIndex];
    
    uint triangleID = vertexID / 3;
    uint vertexInTriangle = vertexID % 3;
    
    uint16_t3 tri = Triangles[mesh.GeometryIdx][triangleID];
    uint16_t triVertex = tri[vertexInTriangle];
    
    Vertex vertex = Vertices[mesh.GeometryIdx][triVertex];

    float3 rootSpacePosition = mul(mesh.Transform, float4(vertex.Position, 1.0f));
    float3 worldSpacePosition = mul(instance.Transform, float4(rootSpacePosition, 1.0f));

    float4 clipSpacePosition = mul(Camera.ViewProj, float4(worldSpacePosition - Camera.Position, 1.0));

    float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);
    
    o.Position = clipSpacePosition;
    o.WorldPosition = worldSpacePosition;
    o.TexCoord = vertex.Texcoord0;
    o.Normal = normalize(mul(objectToWorld3x3, vertex.Normal));
    o.Bitangent = normalize(mul(objectToWorld3x3, vertex.Bitangent));
    o.Tangent = cross(vertex.Bitangent, vertex.Normal) * vertex.Handedness;
    o.Color = vertex.Color.unpack();
    o.LandBlend0 = vertex.LandBlend0.unpack();
    o.LandBlend1 = vertex.LandBlend1.unpack();    
    o.MeshIndex = meshIndex;
    
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
 
    Surface surface = SurfaceMaker::make(i.WorldPosition, i.TexCoord, i.Normal, i.Tangent, i.Bitangent, i.Color, i.LandBlend0, i.LandBlend1, mesh);
    
    Material material = mesh.Material;
    
    if (material.AlphaFlags & AlphaFlags::Test && surface.Alpha < material.AlphaThreshold)
        discard;
    
    o.Albedo = float4(surface.Albedo, 1.0f);
    o.NormalRoughness = float4(surface.Normal * 0.5f + 0.5f, surface.Roughness);
    o.EmissiveMetallic = float4(surface.Emissive, surface.Metallic);
    
    return o;
}