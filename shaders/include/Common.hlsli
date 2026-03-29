#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "Include/Common/Game.hlsli"

#define DEPTH_SCALE (0.99998f)

#define FP_VIEW_Z (16.5f)
#define SKY_Z (0.9999f)

#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))

#define DIV_EPSILON (1e-4f)
#define LAND_MIN_WEIGHT (0.01f)

float3 SafeNormalize(float3 input)
{
    float lenSq = dot(input,input);
    return input * rsqrt(max( 1.175494351e-38, lenSq));
}

float3 FlipIfOpposite(float3 normal, float3 referenceNormal)
{
    return (dot(normal, referenceNormal)>=0)?(normal):(-normal);
}

float F0toIOR(float3 F0)
{
	float f0 = max(max(F0.r, F0.g), F0.b);
	return (1.0 + sqrt(f0)) / (1.0 - sqrt(f0));
}

void NormalMap(float3 normalMap, float handedness, float3 geomNormalWS, float3 geomTangentWS, float3 geomBitangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
	normalMap = normalize(normalMap * 2.0f - 1.0f);
	
    normalWS = normalize(normalMap.x * geomTangentWS + normalMap.y * geomBitangentWS + normalMap.z * geomNormalWS);
    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS));
    bitangentWS = cross(normalWS, tangentWS) * handedness;
}

float Remap(float x, float min, float max)
{
    return clamp(min + saturate(x) * (max - min), min, max);
}

float ScreenToViewDepth(const float screenDepth, float4 cameraData)
{
	return (cameraData.w / (-screenDepth * cameraData.z + cameraData.x));
}

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth, const float4 ndcToView)
{
	float3 ret;
	ret.xy = (ndcToView.xy * screenPos.xy + ndcToView.zw) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

half2 EncodeNormal(half3 n)
{
	n = -n;
	half2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
	if (n.z < 0.0)
	{
		#if !defined(DX11)
		p = (1.0 - abs(p.yx)) * select(p >= 0.0, half2(1.0, 1.0), half2(-1.0, -1.0));
		#else
		p = (1.0 - abs(p.yx)) * (p >= 0.0 ? half2(1.0, 1.0) : half2(-1.0, -1.0));
		#endif
	}
	return p * 0.5 + 0.5;
}

half3 DecodeNormal(half2 f)
{
	f = f * 2.0 - 1.0;
	// https://twitter.com/Stubbesaurus/status/937994790553227264
	half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	half t = saturate(-n.z);
	#if !defined(DX11)
	n.xy += select(n.xy >= 0.0, -t, t);
	#else
	n.xy += n.xy >= 0.0 ? -t : t;
	#endif
	return -normalize(n);
}

#endif // COMMON_HLSLI