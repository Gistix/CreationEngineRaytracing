#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"

struct PushConstants
{
	uint GeometryIdx;
};

ConstantBuffer<PushConstants> PC : register(b0);

Texture2D<float4> MSNormalMap : register(t0);
SamplerState MSNSampler : register(s0);

StructuredBuffer<uint16_t3> Triangles[] : register(t0, space1);
StructuredBuffer<Vertex> Vertices[] : register(t0, space2);

struct VSOutput
{
	float4 Position  : SV_POSITION;
	float2 TexCoord0 : TEXCOORD0;
	float3 Normal    : TEXCOORD1;
	float3 Tangent   : TEXCOORD2;
	float3 Bitangent : TEXCOORD3;
};

VSOutput MainVS(in uint vertexID : SV_VertexID)
{
	VSOutput output;

	uint triangleID = vertexID / 3;
	uint vertexInTriangle = vertexID % 3;

	uint16_t3 tri = Triangles[PC.GeometryIdx][triangleID];
	uint16_t triVertex = tri[vertexInTriangle];

	Vertex vertex = Vertices[PC.GeometryIdx][triVertex];

	float2 pos = float2(vertex.Texcoord0) * 2.0f - 1.0f;

	output.Position = float4(pos.x, -pos.y, 1.0, 1.0);
	output.TexCoord0 = vertex.Texcoord0;
	output.Normal = float3(vertex.Normal).xzy;
	output.Tangent = float3(vertex.Tangent).xzy;
	output.Bitangent = cross(float3(vertex.Normal).xzy, float3(vertex.Tangent).xzy) * vertex.Handedness;

	return output;
}

float4 MainPS(VSOutput input) : SV_Target
{
	float4 msnNormalMap = MSNormalMap.SampleLevel(MSNSampler, input.TexCoord0, 0.0f);
	float3 msNormals = normalize(msnNormalMap.xyz * 2.0f - 1.0f);

	float3 normal = normalize(input.Normal);
	float3 tangent = normalize(input.Tangent);
	float3 bitangent = normalize(input.Bitangent);

	float3x3 tbn = float3x3(tangent, bitangent, normal);

	float3 tangentNormal = mul(tbn, msNormals - normal);
	tangentNormal.z = sqrt(saturate(1.0f - dot(tangentNormal.xy, tangentNormal.xy)));

	return float4(tangentNormal * 0.5f + 0.5f, msnNormalMap.w);
}
